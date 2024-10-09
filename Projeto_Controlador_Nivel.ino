// Projeto Controlador de Nível
// Autor: Felipe Figueiredo Bezerra

//---------------------------------------//
// Resumo
//---------------------------------------//

//       i²C      i²C        GPIO
// ESP32 <-> OLED <-> PCF8547 <-> Joystick

// 1. Através da comunicação I2C do PCF8574, receber os comandos de teclado via Joystick 5 Eixos
// 2. Através do potenciometro, simular a resposta em frequencia (Hz) do sensor de vazão YF-S201
// 3. Exibir as informações de vazao (atual, acumulada) no display OLED

// Bibliotecas
#include <Wire.h>               // Comunicação I2C
#include <PCF8574.h>            // PCF8574
#include <Adafruit_SSD1306.h>   // Display OLED
#include <Adafruit_GFX.h>       // Display OLED

// Atribuições
#define PCF8574_LOW_LATENCY //Define o PCF8475 no modo de trabalho de baixa latência

// SSD1306 pixel code generator
//https://arduinogfxtool.netlify.app/#:~:text=Online%20Arduino%20AdafruitGFX%20SSD1306%20OLED%20Code%20Generator.%20For

// SSD1306 OLED
#define LarguraTela 128   // x
#define AlturaTela 64     // y

// GPIO
#define pcf8574Int 23      // Pino de interrupção do PCF8574 (ativo em nível ZERO)
#define builtinLed 2       // Led onboard ESP32 Wroom shield

// LED RGB
#define ledRed 19
#define ledGreen 18
#define ledBlue 26 // Modificado o GPIO 5 para o GPIO 26, pois ao resetar o ESP32, o GPIO 5 vai para o nível lógico UM

// Gerador de Onda Quadrada (Oscilador)
// Observação:
// * É utilizado o builtin led (GPIO 2) como saída digital para geração de onda quadrada
// * É utilizado o GPIO 4 para receber os pulsos gerados pelo GPIO 2

#define contadorPulsosInt 4  // Pino de interrupção para contar a quantidade de pulsos recebida do YF-S201

// PCF8574 I2C Addressing Map (8 devices max)
// https://github.com/xreef/PCF8574_library
// ------------------------
// Address | A0 | A1 | A2 |
//  0x20   | 0  | 0  | 0  |
//  0x21   | 0  | 0  | 1  |
//  0x22   | 0  | 1  | 0  |
//  0x23   | 0  | 1  | 1  |
//  0x24   | 1  | 0  | 0  |
//  0x25   | 1  | 0  | 1  |
//  0x26   | 1  | 1  | 0  |
//  0x27   | 1  | 1  | 1  |
// ------------------------

//---------------------------------------//
// Objetos
//---------------------------------------//

PCF8574 ioExpander_1(0x20);   // Objeto PCF8547 no endereço 0x20
PCF8574::DigitalInput di;     // Objeto 'di' para obter o status de cada GPIO do PCF8574

Adafruit_SSD1306 oled(LarguraTela, AlturaTela, &Wire, -1); // Objeto 'oled' do tipo 'Adafruit_SSD1306'

hw_timer_t *Tempo = NULL; // Timer

//---------------------------------------//
// Variáveis
//---------------------------------------//

String joystickBotaoPressionado;                // Nome do botão pressionado (Up, Down, Left, Right, Set e Reset)
volatile bool exibirBotaoPressionado = false;   // Flag de evento, para somente quando algum botao do Joystick for acionado

unsigned volatile long quantidadePulsos = 0;
volatile bool flagLerFrequenciaPulsos = false;

float vazaoInstLitroHora = 0;      // Vazao instantanea em L/h
float vazaoInstMlSegundo = 0;      // Vazao instantanea em ml/s
float vazaoAcumulada = 0;
int freqInst = 0;

//variables to keep track of the timing of recent interrupts
volatile unsigned long buttonTime = 0;  
volatile unsigned long lastButtonTime = 0;

int valorADC = 0;         // Valor do canal ADC (0-4096)
int frequencia = 0;   // Valor calculado com base na leitura do canal ADC

//---------------------------------------//
// Interrupções - ISR
//---------------------------------------//

// Joystick - Interrupt Service Routine
void IRAM_ATTR IsrJoystick() 
{
  // Software Debounce
  buttonTime = millis();
  
  if((buttonTime - lastButtonTime) > 900)
  {
    Serial.println("\nISR Joystick");
    exibirBotaoPressionado = true;
    lastButtonTime = buttonTime;
  }

}

// ContadorPulsos YF-S201 - Interrupt Service Routine
void IRAM_ATTR IsrMedidorVazao()
{
  // Realiza a contagem da quantidade de pulsos
  //Serial.println("\nISR YF-S201");
  //Serial.println(quantidadePulsos);
  quantidadePulsos++;
}

// Timer 1s
void IRAM_ATTR IsrTempo()
{
  // Realiza a leitura, durante 1s, da quantidade de pulsos recebida no GPIO 4
  // O valor recebido é em Hertz [Hz]
  // Explicação:
  // fout = 1 / T
  // fout = 1 / 1
  // fout = quantidadePulsos (GPIO 4)

  //Serial.println("\nISR Timer");
  flagLerFrequenciaPulsos = true;
}

//---------------------------------------//
// Métodos
//---------------------------------------//

void FrequenciaMedidorVazao()
{
  // Se o flag acionado pelo ISR MedidorVazao
  if(flagLerFrequenciaPulsos)
  {
    // Realiza o cálculo da vazao instantanea em Litros/Hora
    VazaoInstantanea();

    Serial.printf("\n[P]%u [V L/H]%.2f", quantidadePulsos, vazaoInst);

    quantidadePulsos = 0;
    flagLerFrequenciaPulsos = false;
  }
}

void VazaoInstantanea()
{
  // Datasheet
 
  // Vazao [L/H] | Freq [Hz] |
  //    120      |    16     |
  //    240      |    32.5   |
  //    360      |    49.3   |
  //    480      |    65.5   |
  //    600      |    82     |
  //    720      |    90.2   |

  // Aplicando a regressão linear (Calculadora Gráfica)
  // https://www.desmos.com/calculator?lang=pt-BR
  
  // Resposta: --> [y = 0.1275x + 2.347]

  // Substituindo os valores de x e y...
  // --> Freq[Hz] = 0.1275 * Vazao[L/H] + 2.347
  
  // Logo:
  // --> Vazao[L/H] = (Freq[Hz] - 1.49) / 0.128
  
  vazaoInstLitroHora = (quantidadePulsos - 1.49) / 0.128; // Unidade: L/h
  vazaoInstMlSegundo = (vazaoInstLitroHora / 3600) * 1000;
 
}

void FrequenciaInstantanea()
{
  // y = 0.1275x + 2.347
  // Freq[Hz] = 0.1275 * Vazao[L/H] + 2.347

}

void MenuVazaoInstantanea()
{
  oled.clearDisplay();

  // Linha superior
  oled.setCursor(0,0);             // Start at top-left corner
  oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text
  oled.println(F("Vazao inst. (L/H)"));

  // OBS: A divisão entre as cores Amarelo e Azul é equivalente á y0 = 17
  // Retangulo
  oled.drawRect(2, 17, 124, 19, SSD1306_WHITE);

  // Exibe informações no OLED
  oled.display();
  //delay(5);
}

void MenuPrincipal()
{
  oled.clearDisplay();

  // Linha superior
  oled.setCursor(0,0);             // Start at top-left corner
  oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text
  oled.println(F("Vazao inst. (L/H)"));

  // OBS: A divisão entre as cores Amarelo e Azul é equivalente á y0 = 17
  // Retangulo
  oled.drawRect(2, 17, 124, 19, SSD1306_WHITE);

  // Exibe informações no OLED
  oled.display();
  //delay(5);
}

void TestarLedRgb()
{
  // Escrever com o valor ZERO
  digitalWrite(ledRed, LOW);
  digitalWrite(ledGreen, LOW);
  digitalWrite(ledBlue, LOW);
  delay(100);

  // Testar cada led
  // Testando o led Vermelho
  digitalWrite(ledRed, HIGH);
  delay(500);
  digitalWrite(ledRed, LOW);
  delay(500);

  // Testando o led Verde
  digitalWrite(ledGreen, HIGH);
  delay(500);
  digitalWrite(ledGreen, LOW);
  delay(500);

  // Testando o led Azul
  digitalWrite(ledBlue, HIGH);
  delay(500);
  digitalWrite(ledBlue, LOW);
  delay(500);

  // Testar todos os Leds
  digitalWrite(ledRed, HIGH);
  delay(500);
  digitalWrite(ledGreen, HIGH);
  delay(500);
  digitalWrite(ledBlue, HIGH);
  delay(500);

  // Escrever com o valor ZERO
  digitalWrite(ledRed, LOW);
  digitalWrite(ledGreen, LOW);
  digitalWrite(ledBlue, LOW);
  delay(100);

}



void ReadPcf8574Inputs()
{
  di = ioExpander_1.digitalReadAll();

  if(di.p1 == LOW)
  {
    joystickBotaoPressionado = "Up";
  }
  else if(di.p2 == LOW)
  {
    joystickBotaoPressionado = "Down";
  }
  else if(di.p3 == LOW)
  {
    joystickBotaoPressionado = "Left";
  }
  else if(di.p4 == LOW)
  {
    joystickBotaoPressionado = "Right";
  }
  else if(di.p5 == LOW)
  {
    joystickBotaoPressionado = "Mid";
  }
  else if(di.p6 == LOW)
  {
    joystickBotaoPressionado = "Set";
  }
  else if(di.p7 == LOW)
  {
    joystickBotaoPressionado = "Reset";
  }

  // Quando a interrupção ocorre, então exibir qual o botão foi pressionado
  if(exibirBotaoPressionado)
  {
    Serial.printf("Joystick: %s", joystickBotaoPressionado);
    exibirBotaoPressionado = false;
  }

}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("Hello, ESP32!");

  // PinMode 'ESP32'
  // Entradas

  // Saidas
  pinMode(builtinLed, OUTPUT); // builtin led
  pinMode(ledRed, OUTPUT);     // led RGB
  pinMode(ledGreen, OUTPUT);   // led RGB
  pinMode(ledBlue, OUTPUT);    // led RGB

  // PinMode 'PCF8574'
  // Entradas
  ioExpander_1.pinMode(P1, INPUT_PULLUP); // UP
  ioExpander_1.pinMode(P2, INPUT_PULLUP); // DOWN
  ioExpander_1.pinMode(P3, INPUT_PULLUP); // LEFT
  ioExpander_1.pinMode(P4, INPUT_PULLUP); // RIGTH
  ioExpander_1.pinMode(P5, INPUT_PULLUP); // MID
  ioExpander_1.pinMode(P6, INPUT_PULLUP); // SET
  ioExpander_1.pinMode(P7, INPUT_PULLUP); // RESET

  // Saidas
  ioExpander_1.pinMode(P0, OUTPUT); // COM

  ioExpander_1.begin();

  // Realizar a varredura do 'joystick 5 eixos'
  // enviando o pino 'COM' para LOW
  ioExpander_1.digitalWrite(P0, LOW);

  // Interrupçoes
  // attachInterrupt(GPIOPin, ISR, Mode);

  // Onde:
  // - GPIO, define o pino da interrupção
  // - ISR, nome da função que executa qdo a interrupção ocorre
  // - Mode, define quando a interrupção deve ser ocorrida
  //    1. LOW (qdo o pino é nível 0)
  //    2. HIGH (qdo o pino é nível 1)
  //    3. CHANGE (qdo o nível do pino é alterado)
  //    4. FALLING (qdo o pino vai do estado 1 para o estado 0)
  //    5. RISING (qdo o pino vai do estado 0 para o estado 1)
  attachInterrupt(digitalPinToInterrupt(pcf8574Int), IsrJoystick, FALLING);     // Joystick - Interrupt Service Routine
  attachInterrupt(digitalPinToInterrupt(contadorPulsosInt), IsrMedidorVazao, FALLING); // Medidor de Vazao - Interrupt Service Routine

  delay(50);

  // SSD1306_SWITCHCAPVCC 
  // generate display voltage from 3.3V internally
  if(!(oled.begin(SSD1306_SWITCHCAPVCC,0x3C)))
  {
   Serial.println("OLED SSD1306 allocation failed.");
   for(;;); // Don't proceed, loop forever
  }

  delay(1000);

  // Limpar o display
  oled.clearDisplay();
  delay(1000);

  // Testar o led RGB
  TestarLedRgb();

  // Inicializar o Timer
  Tempo = timerBegin(1000000);             // Inicializa o timer com o parametro de 1000000
  timerAttachInterrupt(Tempo, &IsrTempo);  // Anexa o timer 'Tempo' ao serviço de interrupção ISR
  timerAlarm(Tempo, 1000000 * 1, true, 0);   // Determina quando haverá a interrupção (no caso, á cada 1s)

  //timerWrite(Tempo, 0); // Reseta o timer
  
}

void loop() {
  // put your main code here, to run repeatedly:
  //delay(20);

  // Realiza a leitura das entradas do PCF8574
  ReadPcf8574Inputs();

  // Realiza a leitura da frequencia de pulsos recebida do Medido de Vazao durante o intervalo de 1s
  FrequenciaMedidorVazao();

  

  MenuPrincipal();

}
