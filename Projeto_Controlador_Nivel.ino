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
// * É utilizado, de um outro ESP32, o builtin led (GPIO 2) como saída digital para geração de onda quadrada
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
volatile int eventosInterrupcao = 0;            // Variável que realiza o debounce do INT do PCF8574

bool botaoUp = false;
bool botaoDown = false;
bool botaoLeft = false;
bool botaoRight = false;
bool botaoMid = false;
bool botaoSet = false;
bool botaoReset = false;

unsigned volatile long quantidadePulsos = 0;
volatile bool flagLerFrequenciaPulsos = false;
volatile bool flagIgnorarPulsos = false;

float vazaoInstLitroHora = 0;         // Vazao instantanea em L/h
float vazaoInstLitroSegundo = 0;      // Vazao instantanea em L/s
float vazaoInstMililitroSegundo = 0;  // Vazao instantanea em mL/s

float vazaoAcumuladaLitro = 0;      // Vazao acumulada em L
float vazaoAcumuladaMililitro = 0;  // Vazao acumulada em mL

int freqInst = 0;

//variables to keep track of the timing of recent interrupts
volatile unsigned long buttonTime = 0;  
volatile unsigned long lastButtonTime = 0;

int valorADC = 0;         // Valor do canal ADC (0-4096)
int frequencia = 0;       // Valor calculado com base na leitura do canal ADC

volatile bool flagAtualizaOled = false;
int menuPagina = 0;
int menuLinha = 0;

float nivelSP = 0;
bool flagNivelSP = false;     // Indica que o valor do Setpoint foi alcancado
bool statusProcesso = false;  // Indicador do processo (RUN, STOP)

int valorBarraProgresso = 0;
volatile bool alreadyDraw = false;

int horas = 0;
int minutos = 0;
int segundos = 0;

//---------------------------------------//
// Interrupções - ISR
//---------------------------------------//

// Joystick - Interrupt Service Routine
void IRAM_ATTR IsrJoystick() 
{
  // Software Debounce
  buttonTime = millis();
  
  if((buttonTime - lastButtonTime) > 50)
  {
    //Serial.println("\nISR Joystick");
    eventosInterrupcao++;
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

  flagAtualizaOled = true;
}

//---------------------------------------//
// Métodos
//---------------------------------------//

// Realiza a leitura da frequencia de pulsos recebida do Medido de Vazao durante o intervalo de 1s
void FrequenciaMedidorVazao()
{
  // Se o flag acionado pelo ISR MedidorVazao
  if(flagLerFrequenciaPulsos && !flagIgnorarPulsos)
  {
    
    VazaoInstantanea();       // Realiza o cálculo da vazao instantanea em Litros/Hora
    VazaoAcumulada();         // Realiza a soma acumulativa da vazao instantanea em Litros/Segundo (pois o polling rate é 1 segundo)
    FrequenciaInstantanea();  // Realiza o calculo da frequencia instantanea do YF-S201

    // Serial.printf("\n[P]%u  [Vinst]%.2f L/h  [Vinst]%.4f L/s  [Vinst]%.2f mL/s  [Vacc]%.2f L", 
    //                 quantidadePulsos,
    //                 vazaoInstLitroHora,
    //                 vazaoInstLitroSegundo,
    //                 vazaoInstMililitroSegundo,
    //                 vazaoAcumuladaLitro);
    quantidadePulsos = 0;

    if(statusProcesso)
    {
      segundos++;
    }

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
 
  // Evitar divisao por ZERO
  if(vazaoInstLitroHora > 0)
  {
    vazaoInstLitroSegundo = (vazaoInstLitroHora / 3600);       // Unidade: L/s
    vazaoInstMililitroSegundo = vazaoInstLitroSegundo * 1000;  // Unidade: mL/s
  }
  else
  {
    vazaoInstLitroHora = 0;
    vazaoInstLitroSegundo = 0;
    vazaoInstMililitroSegundo = 0;
  }

}

void VazaoAcumulada()
{
  // Realiza a soma acumulativa da vazão em Litro/Segundo
  vazaoAcumuladaLitro += vazaoInstLitroSegundo;
}

void FrequenciaInstantanea()
{
  // y = 0.1275x + 2.347
  // Freq[Hz] = 0.1275 * Vazao[L/H] + 2.347
  freqInst = 0.1275 * vazaoInstLitroHora + 2.437;

  // Devido ao ruído no GPIO 4, considerar o erro de 1% (2Hz)
  if(freqInst <= 2)
  {
    freqInst = 0;
  }

}

void ControleAutomacao()
{

  // Se a vazaoAcumulada for maior ou igual ao SetPoint de Nível
  if(nivelSP != 0 && vazaoAcumuladaLitro >= nivelSP)
  {
    flagIgnorarPulsos = true;  // Nao contabilizar os pulsos recebidos no ISR INT
    statusProcesso = false;     // Sinalizar que o processo está em modo STOP
    flagNivelSP = true;
  }
  
  if(statusProcesso)
  {
    // Sinaliza o acionamento do motor via led rgb
    digitalWrite(ledRed, LOW);
    digitalWrite(ledGreen, HIGH);
    digitalWrite(ledBlue, LOW);

    alreadyDraw = false;
  }
  else
  {
    // Sinaliza o acionamento do motor via led rgb
    digitalWrite(ledRed, HIGH);
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledBlue, LOW);

    alreadyDraw = false;
  }
  
}

// Controla a informação do tempo enquanto o processo está no modo RUN
void TempoStatusRun()
{
  if(segundos > 59)
  {
    segundos = 0;
    minutos++;

    if(minutos > 59)
    {
      minutos = 0;
      horas++;

      if(horas > 23)
      {
        segundos = 0;
        minutos = 0;
        horas = 0;
      }
    }
  }
  
}

void ExibirTempoDecorrido()
{

  // Controla a formatação do texto de 'relogio' no modo RUN
  TempoStatusRun();

  // Como apagar somente uma determinada regiao
  // desenhar um fillRectangle invertido
  oled.fillRect(0, 0, 80, 12, SSD1306_BLACK);   // Apaga somente a linha superior
  oled.fillRect(0, 36, 128, 24, SSD1306_BLACK); // Apaga o conteudo do tempo decorrido

  // Linha superior
  oled.setCursor(0,0);             // Start at top-left corner
  oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text
  oled.println(F("Duracao:"));

  // Nível Acumulado
  oled.setCursor(10, 45);
  oled.setTextSize(2);             
  oled.setTextColor(SSD1306_WHITE);
  oled.print(horas);
  oled.print(":");
  oled.print(minutos);
  oled.print(":");
  oled.print(segundos);
  oled.print("s");

  // Exibe informações no OLED
  oled.display();

}

void ExibirVazaoInstantanea()
{
  // Como apagar somente uma determinada regiao
  // desenhar um fillRectangle invertido
  oled.fillRect(0, 0, 80, 12, SSD1306_BLACK);   // Apaga somente a linha superior
  oled.fillRect(0, 39, 128, 24, SSD1306_BLACK); // Apaga o conteudo da vazao instantanea

  // Linha superior
  oled.setCursor(0,0);             // Start at top-left corner
  oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text
  oled.println(F("Vazao Inst:"));

  // Nível Setpoint
  oled.setCursor(90, 37);
  oled.setTextSize(1);             
  oled.setTextColor(SSD1306_WHITE);
  oled.print(nivelSP);
  oled.print("L");

  // Alternar exibições entre as unidades:
  // 1. vazao instantanea (ml/s)
  // 2. vazao instantanea (L/s)
  // 3. vazao instantanea (L/h)
  // 4. frequencia do medidor de vazao YF-S201

  if(menuPagina == 1)
  {
    // Definir os limites de navegacao
    if(menuLinha < 0)
    {
      menuLinha = 0;
    }
    else if(menuLinha > 3)
    {
      menuLinha = 3;
    }

    switch(menuLinha)
    {
      case 0:
        oled.setCursor(2, 45);
        oled.setTextSize(2);             
        oled.setTextColor(SSD1306_WHITE);
        oled.print(vazaoInstMililitroSegundo);
        oled.print("ml/s");
        break;

      case 1:
        oled.setCursor(2, 45);
        oled.setTextSize(2);             
        oled.setTextColor(SSD1306_WHITE);
        oled.print(vazaoInstLitroSegundo);
        oled.print("L/s");
        break;

      case 2:
        oled.setCursor(2, 45);
        oled.setTextSize(2);             
        oled.setTextColor(SSD1306_WHITE);
        oled.print(vazaoInstLitroHora);
        oled.print("L/h");
        break;

      case 3:
        oled.setCursor(2, 45);
        oled.setTextSize(2);             
        oled.setTextColor(SSD1306_WHITE);
        oled.print(freqInst);
        oled.print("Hz");
        break;
    }
  }

  // Exibe informações no OLED
  oled.display();
}

void ExibirNivelSetpoint()
{
  // Como apagar somente uma determinada regiao
  // desenhar um fillRectangle invertido
  oled.fillRect(0, 0, 80, 12, SSD1306_BLACK);   // Apaga somente a linha superior
  oled.fillRect(0, 12, 127, 63, SSD1306_BLACK); // Apaga o conteúdo da informação sobre o nível acumulado 
  
  // Linha superior
  oled.setCursor(0,0);             // Start at top-left corner
  oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text
  oled.println(F("SP Nivel:"));

  // Nível Acumulado
  oled.setCursor(30, 45);
  oled.setTextSize(2);             
  oled.setTextColor(SSD1306_WHITE);
  oled.print(nivelSP);
  oled.print("L");

  // Exibe informações no OLED
  oled.display();


}

void ExibirNivelAcumulado()
{
  // Como apagar somente uma determinada regiao
  // desenhar um fillRectangle invertido
  oled.fillRect(0, 0, 80, 12, SSD1306_BLACK);   // Apaga somente a linha superior
  oled.fillRect(0, 38, 127, 63, SSD1306_BLACK); // Apaga o conteúdo da informação sobre o nível acumulado 

  // Linha superior
  oled.setCursor(0,0);             // Start at top-left corner
  oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text
  oled.println(F("Nivel Total:"));

  // Retangulo
  oled.drawRect(2, 16, 124, 18, SSD1306_WHITE);

  // Nível Setpoint
  oled.setCursor(90, 37);
  oled.setTextSize(1);             
  oled.setTextColor(SSD1306_WHITE);
  oled.print(nivelSP);
  oled.print("L");

  // Nível Acumulado
  oled.setCursor(4, 45);
  oled.setTextSize(2);             
  oled.setTextColor(SSD1306_WHITE);
  oled.print(vazaoAcumuladaLitro);
  oled.print("L");

  // Exibe informações no OLED
  oled.display();
}

void MenuPrincipal()
{
  oled.clearDisplay();

  // Linha superior
  oled.setCursor(0,0);             // Start at top-left corner
  oled.setTextSize(1);             // Normal 1:1 pixel scale
  oled.setTextColor(SSD1306_WHITE);        // Draw white text
  oled.println(F("Nivel Total:"));

  // Retangulo
  oled.drawRect(2, 16, 124, 18, SSD1306_WHITE);

  // Exibe informações no OLED
  oled.display();
  //delay(5);
}

// Exibe a mensagem de RUN ou STOP
void ExibirStatusProcesso()
{
  // Como apagar somente uma determinada regiao
  // desenhar um fillRectangle invertido
  oled.fillRect(85, 0, 128, 12, SSD1306_BLACK);   // Apaga somente o texto RUN OU STOP

  if(statusProcesso)
  {
    // Texto 'RUN'
    oled.setCursor(90, 0);             // Start at top-left corner
    oled.setTextSize(1);             // Normal 1:1 pixel scale
    oled.setTextColor(SSD1306_WHITE);        // Draw white text
    oled.println(F(" RUN "));
  }
  else
  {
    // Texto 'STOP'
    oled.setCursor(90, 0);             // Start at top-left corner
    oled.setTextSize(1);             // Normal 1:1 pixel scale
    oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);        // Draw white text
    oled.println(F(" STOP "));
  }

  // Exibe informações no OLED
  oled.display();
}

void ExibirBarraProgresso()
{
  // SP  | Progresso (px) |
  // 10  |    120         |
  // 0   |    4           |

  // Evitar a divisao por ZERO
  if(nivelSP > 0)
  {
    valorBarraProgresso = 120 * vazaoAcumuladaLitro / nivelSP;
  }
  else
  {
    valorBarraProgresso = 0;
  }

  // Retangulo 100%
  //oled.fillRect(4, 18, 120, 14, SSD1306_WHITE);

  // Desenhar a barra de progresso
  oled.fillRect(4, 18, valorBarraProgresso, 14, SSD1306_WHITE);

  // Exibe informações no OLED
  oled.display();

  //Serial.printf("\n[Valor Pixel]%i", valorBarraProgresso);

}

void MenuNavegacao()
{
  if(flagAtualizaOled) // Atualiza o conteúdo do OLED a cada 1s
  {
    if(menuPagina < 0)
    {
      menuPagina = 0;
    }
    else if(menuPagina > 2 && menuPagina < 10)
    {
      menuPagina = 2;
    }

    // Exibe o status do processo caso houver modificação
    if(!alreadyDraw)
    {
      ExibirStatusProcesso();
      alreadyDraw = true;
    }

    ExibirBarraProgresso();

    switch(menuPagina)
    {
      case 0:
        ExibirNivelAcumulado();
        break;

      case 1:
        ExibirVazaoInstantanea();
        break;

      case 2:
        ExibirTempoDecorrido();
        break;

      case 10:
        ExibirNivelSetpoint();
        break;

    }

    flagAtualizaOled = false; // Reset do flag
  }
  

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
  delay(20);
  
  if(di.p1 == LOW)
  {
    joystickBotaoPressionado = "Up";

    if(menuPagina == 10)
    {
      // Define o status do processo como STOP
      statusProcesso = false;

      nivelSP = nivelSP + 0.1;

      // Previnir o overflow
      if(nivelSP > 9999)
      {
        nivelSP = 9999;
      }
    }
  }
  else if(di.p2 == LOW)
  {
    joystickBotaoPressionado = "Down";

    if(menuPagina == 10)
    {
      // Define o status do processo como STOP
      statusProcesso = false;

      nivelSP = nivelSP - 0.1;

      // Previnir o overflow
      if(nivelSP < 0)
      {
        nivelSP = 0;
      }
    }
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
  else
  {
    joystickBotaoPressionado = "None";
  }

  // O PCF8574 registra dois eventos de INT á cada acionamento individual do joystick
  // Exemplo:
  // ISR Mid (INT-1) Botao pressionado <-- Realizar a leitura nesse evento
  // ISR Mid (INT-2) Botao solto

  // Quando a interrupção ocorre, então exibir qual o botão foi pressionado
  if(exibirBotaoPressionado && eventosInterrupcao == 1)
  {
    // Obter a leitura do botao do joystick somente no evento de RISING (retorno - solto)
    // O termo 'None' é quando o botão físico do joystick está no evento de FALLING (pressionado)
    if(joystickBotaoPressionado != "None") 
    {
      Serial.printf("\nJoystick: %s", joystickBotaoPressionado);

      // Realiza o incremento através do botão de função atribuida
      if(joystickBotaoPressionado == "Up")
      {
        menuLinha--;
      }
      else if(joystickBotaoPressionado == "Down")
      {
        menuLinha++;
      }
      else if(joystickBotaoPressionado == "Left")
      {
        menuPagina--;
      }
      else if(joystickBotaoPressionado == "Right")
      {
        menuPagina++;
      }
      else if(joystickBotaoPressionado == "Mid")
      {
        // Se o cursor estiver na pagina zero
        if(menuPagina == 0)
        {
          // Entao apresenta a pagina que define o valor do SetPoint
          menuPagina = menuPagina + 10;
        }
        else if(menuPagina == 10) // Se estiver na pagina que define o SetPoint
        {
          menuPagina = 0; // Entao retorna a pagina anterior
        }
      }
      else if(joystickBotaoPressionado == "Set")
      {
        // Permitir iniciar o processo somente se o SetPoint for maior que ZERO
        if(nivelSP > 0 && flagNivelSP == false)
        {
          statusProcesso = true;
          alreadyDraw = false;
          flagIgnorarPulsos = false;
        }
        else
        {
          Serial.println("\n[INFO] Não foi possível iniciar o processo pois PV = SP.");
        }
        
      }
      else if(joystickBotaoPressionado == "Reset")
      {
        statusProcesso = false;
        alreadyDraw = false;
        flagIgnorarPulsos = true;
      }

      // Debug para acompanhamento
      Serial.printf("\n[Pag]%i  [Linha]%i", menuPagina, menuLinha);
      
    }

   eventosInterrupcao = 0; 
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

  // Exibir o Menu Principal (estático) no display
  MenuPrincipal();

  // Reset do contador de eventos de interrupcao (Bugfix)
  eventosInterrupcao = 0;
  
}

void loop() {
  // put your main code here, to run repeatedly:
  //delay(20);

  // Realiza a leitura das entradas do PCF8574
  ReadPcf8574Inputs();

  // Realiza a leitura da frequencia de pulsos recebida do Medido de Vazao durante o intervalo de 1s
  FrequenciaMedidorVazao();

  // Controle de navegação e exibição de Menus
  MenuNavegacao();

  // Controle de automação
  ControleAutomacao();

}
