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
#define potenciometro 34   // Pino do canal ADC que recebe a variação de tensão do potenciometro
#define builtinLed 2       // Led onboard ESP32 Wroom shield

// LED RGB
#define ledRed 19
#define ledGreen 18
#define ledBlue 5

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

//---------------------------------------//
// Variáveis
//---------------------------------------//

String joystickBotaoPressionado;                // Nome do botão pressionado (Up, Down, Left, Right, Set e Reset)
volatile bool exibirBotaoPressionado = false;   // Flag de evento, para somente quando algum botao do Joystick for acionado

unsigned volatile long quantidadePulsos = 0;

//variables to keep track of the timing of recent interrupts
volatile unsigned long buttonTime = 0;  
volatile unsigned long lastButtonTime = 0;

int valorADC = 0;         // Valor do canal ADC (0-4096)
int frequencia = 0;   // Valor calculado com base na leitura do canal ADC

float periodoParcial = 0.0000;
unsigned long tempoAgora = 0;
unsigned long tempoAnterior = 0;  

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
  // [Determinar o polling rate]

  Serial.println("\nISR YF-S201");
  Serial.println(quantidadePulsos);
  quantidadePulsos++;
}

//---------------------------------------//
// Métodos
//---------------------------------------//

void LerValorADC()
{
  // Realiza a leitura do ADC para o GPIO potenciometro
  valorADC = analogRead(potenciometro);

  // Mapping
  // ADC Bits: 0-4095
  // Frequencia YF-S201: 232Hz (consulte o calculo abaixo)
  
  // Aproximação - via regra de três
  // 720L/H --> 90.2 Hz
  // 1800 L/H --> x? Hz

  // x = (90.2 * 1800) / 720
  // [x = 225.5Hz (max) @1800L/H ou 30L/min]

  // Aproximação - via polinomial
  // Freq[Hz] = 0.1275 * Vazao[L/H] + 2.347
  // Freq = 0.1275 * 1800 + 2.347
  // [Freq = 231.85 Hz]
  // [Freq ~= 232 Hz (max)]

  // Erro
  // Erro = 225.5 - 231.85
  // [Erro = +-6,35Hz ou +-2,74%]

  // Linearização de valores via map()
  frequencia = map(valorADC, 0, 4095, 0, 232);

  // De acordo com a frequencia obtida, entao gerar a onda quadrada
  GerarOndaQuadrada(frequencia); 

  //Serial.printf("\n[ADC]%i [Hz]%i", valorADC, frequencia);

}

void GerarOndaQuadrada(float valorFrequencia)
{ 
  //     _____       _____
  //    |     |     |     |
  //    |     |_____|     |_____
  //    
  //    |---T(ms)---|
  //    |-----|-----|
  //      50%   50%
  
  // Freq[Hz] = 1 / T[s]
  // Logo:  
  // --> [T = 1 / Freq]

  // [YF-S201]
  // Hz  | T       | T/2           |
  // 231 | 4.329ms | 2.164ms [max] |
  // 1   | 1000ms  | 500ms   [min] |

  // Evitar a divisão por ZERO (INF)
  if(valorFrequencia > 0)
  {
    //Calcular o periodo considerando o duty cycle de 50%
    periodoParcial = ((1 / valorFrequencia) / 2) * 1000; // Resposta em Milisegundos (ms)

    // Obtem o tempo atual
    tempoAgora = millis();
    
    // Se o tempo decorrido for maior igual ao 'periodoParcial'
    if(tempoAgora - tempoAnterior >= periodoParcial)
    {
      digitalWrite(builtinLed, !digitalRead(builtinLed));
      tempoAnterior = tempoAgora;
    }
  }
  else
  {
    periodoParcial = 0;
    digitalWrite(builtinLed, LOW);
  }

  //Serial.printf("\n[ADC]%i [Hz]%i [T/2]%.2f ms", valorADC, frequencia, periodoParcial);

}

void ObterVazao()
{
  // y = 0.1275x + 2.347
  // Freq[Hz] = 0.1275 * Vazao[L/H] + 2.347
  // Logo:
  // Vazao[L/H] = (Freq[Hz] - 1.49) / 0.128

}

void ObterFrequencia()
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

}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("Hello, ESP32!");

  // ADC
  // (0-4095 bits) --> (0V - 3.3V)
  analogReadResolution(12);

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

}

void loop() {
  // put your main code here, to run repeatedly:
  //delay(20);

  // Realiza a leitura das entradas do PCF8574
  ReadPcf8574Inputs();
  
  // Quando a interrupção ocorre, então exibir qual o botão foi pressionado
  if(exibirBotaoPressionado)
  {
    Serial.printf("Joystick: %s", joystickBotaoPressionado);
    exibirBotaoPressionado = false;
  }

  // Realiza a leitura do canal ADC ESP32
  LerValorADC();

  MenuPrincipal();

}
