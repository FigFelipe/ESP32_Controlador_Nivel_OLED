# ESP32 - Projeto Controlador Nível
Projeto apresentado no curso de IoT com ESP32 na UTD - Universidade do Trabalho Digital do Ceará. 

## Objetivo
Realizar, de modo didático, o controle de nível com o ESP32 integrando os periféricos como Medidor de Vazão YF-S201, display OLED, PCF8574 e um Joystick de botões.

## Autor
- [Felipe Figueiredo Bezerra](https://github.com/FigFelipe)

## Ambiente de Desenvolvimento

 - **IDE**: Arduino IDE 2.3.3
 - **MCU:** Espressif ESP32-Wroom Dev Module

## Dispositivos

### 1. Módulo Joystick 5 Eixos
O módulo Joystick de 5 eixos é um botão de multifunção, qual funciona como um push-button normal aberto, disponibilizando no total 5 funções de direção (Up, Down, Left, Right, Mid). Além disso, o módulo também possui 2 botões individuais do tipo push-button normal aberto (Set e Reset):

| Terminais             | Descrição |
|-----------------------|-----------|
| COM                   | Comum (para todos os botões do módulo) |
| UP                    | Joystick Push Button NO |
| DWN                   | Joystick Push Button NO |
| LFT                   | Joystick Push Button NO |
| RHT                   | Joystick Push Button NO |
| MID                   | Joystick Push Button NO |
| SET                   | Push Button NO |
| RST                   | Push Button NO |

### 2. Módulo PCF8574
O módulo PCF8574 é um expansor de I/O's multidirecional (funciona tanto como entrada ou saída), sendo controlado especificamente via comunicação I²C.

| Terminais             | Descrição |
|-----------------------|-----------|
| *INT                  | Notifica (enviando nível ZERO) quando os status dos pinos P0:P7 é modificado |
| P0                    | Configurar como Entrada Digital |
| P1                    | Configurar como Saída Digital |
| P2                    | Configurar como Saída Digital |
| P3                    | Configurar como Saída Digital |
| P4                    | Configurar como Saída Digital |
| P5                    | Configurar como Saída Digital |
| P6                    | Configurar como Saída Digital |
| P7                    | Configurar como Saída Digital |
| A0                    | Endereçamento I²C |
| A1                    | Endereçamento I²C |
| A2                    | Endereçamento I²C |

> **Observação:**
O terminal *INT é ativo em nível lógico ZERO, deve ser conectado á um resistor de pull-up (de preferência do valor de 10K Ohm).

O módulo PCF8574 possui o seguinte endereçamento para a comunicação I²C:

| Endereço (HEX)        | A0 | A1 | A2 | 
|-----------------------|----|----|----|
| 0x20                  | 0  | 0  | 0  |
| 0x21                  | 0  | 0  | 1  |
| 0x22                  | 0  | 1  | 0  |
| 0x23                  | 0  | 1  | 1  |
| 0x24                  | 1  | 0  | 0  |
| 0x25                  | 1  | 0  | 1  |
| 0x26                  | 1  | 1  | 0  |
| 0x27                  | 1  | 1  | 1  |
 
### 3. Display OLED 128x64

Por padrão, o módulo de display OLED utiliza o valor de 0x3C como endereçamento i²C: 

| Endereço (HEX)        |  
|-----------------------|
| 0x3C                  | 

### 4. Medidor de Vazão YF-S201

No YF-S201, a variação da frequencia (Hz) de saída determina a vazão instantânea (Litros/Hora). Temos então os dados fornecido no datasheet do YF-S201:

> **Observação:**
O medidor é simulado digitalmente, através da geração de pulsos em um GPIO 2 de um outro ESP32. Para maiores informações, consultar o arquivo 'Projeto_Oscilador.ino'.

| Vazão (L/H) | Freq. (Hz) |
|-------------|------------|
| 120         | 16         |
| 240         | 32,5       |
| 360         | 49,3       |
| 480         | 65,5       |
| 600         | 82         |
| 720         | 90.2       |
| 1800        | 232 (máx)  |

Aplicando o método de regressão linear (considerando um erro de +/-5%), obtemos a seguinte equação característica da resposta do medidor de vazão:

> **y = 0,1275x + 2,347**

### 4.1 Frequência [Hz]
Substituindo os termos x e y na equação, temos:
> **Frequência[Hz]** = ( 0,1275 * Vazão [L/H] ) + 2,347

### 4.2 Vazão [L/H]
Substituindo os termos x e y na equação, temos:
> **Vazão[L/H]** = ( Frequência[Hz] - 2,347 ) / 0,1275

### 4.3 Taxa de Atualização
Será utilizado uma taxa de atualização de 1 segundo (T = 1), onde dentro desse intervalo do período é realizada a contagem da quantidade de pulsos na entrada do GPIO 4 (ISR INT). Portanto, o valor pulsos recebida logo é a frequencia obtida.
> **Freq.[Hz]** = 1 / T



