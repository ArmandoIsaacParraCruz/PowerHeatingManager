#include <Arduino.h>
#include <SPI.h>
#include <avr/wdt.h>

// Configuración de pines SPI en Arduino UNO
// MISO: 12, MOSI: 11, SCK: 13, SS: 10
const int slaveSelectPin = 10;
const uint8_t NUMBER_OF_HEATING_RESISTORS = 6;
const uint8_t MAX_NUMBER_OF_SEMICYCLES = 120;
volatile uint8_t semicyclesPerHeatingResistor[NUMBER_OF_HEATING_RESISTORS];
volatile uint8_t semicyclesCounter;
const uint8_t RECEIVE_SEMICYCLES_FLAG = 255;
const uint8_t HEATING_RESISTORS_PINS[] = {A0, A1, A2, A3, A4, A5};
const uint8_t ZeroCrossingPin = 2;
const uint8_t ledPin = 7;
volatile uint8_t index;
volatile uint8_t dataReceived;
volatile uint32_t protectionTime;
uint32_t ledTime;


/*	
	El enum Status corresponde a los estados en los que se puede encotrar
	el microcontrolador.
*/
enum Status {
  WAITING,					//En espera la siguiente transacción
  STOP,						//Apaga todas las salidas porque el maestro no se ha comunicado
  IN_TRANSACTION,			//Se están recibiendo los valores de semiciclos para cada resistencia
  START_TRANSACTION = 255	//El maestro indica que el micocontrolador se prepare para recibir los valores de los semiciclos
};

enum LedStatus {
  ON,			//Enciende el LED piloto
  OFF			//Apaga el led Piloto
};

LedStatus ledStatus;
Status currentStatus;

// Función para actualizar las salidas
void updateOutputs();

void setup() {
  Serial.begin(115200);
  Serial.println("Init Atmega328P");

  // Inicializar SPI
  SPI.begin();

  // Configurar pines
  pinMode(slaveSelectPin, INPUT);
  pinMode(ZeroCrossingPin, INPUT);
  pinMode(ledPin, OUTPUT);

  // Configurar pines de resistencias de calefacción
  for (uint8_t i = 0; i < NUMBER_OF_HEATING_RESISTORS; ++i) {
    pinMode(HEATING_RESISTORS_PINS[i], OUTPUT);
    semicyclesPerHeatingResistor[i] = 0;
  }

  // Adjuntar interrupción al pin de cruce por cero
  attachInterrupt(digitalPinToInterrupt(ZeroCrossingPin), updateOutputs, FALLING);

  // Adjuntar interrupción SPI
  SPI.attachInterrupt();

  // Habilitar SPI
  SPCR |= _BV(SPE);

  // Configurar estado inicial
  currentStatus = WAITING;
  ledStatus = ON;
  index = 0;

  // Habilitar el Watchdog Timer para un tiempo de espera de 2 segundos
  wdt_enable(WDTO_2S);

  // Inicializar el contador de semiciclos y tiempo del LED
  semicyclesCounter = 0;
  ledTime = millis();
}

void loop() {
  /* 
  	Alternar el estado del LED cada segundo para saber que el microcontrolador
   	sigue en funcionamiento.
   */
  if (millis() - ledTime >= 1000) {
    if (ledStatus == ON) {
      digitalWrite(ledPin, HIGH);
      ledStatus = OFF;
    } else {
      digitalWrite(ledPin, LOW);
      ledStatus = ON;
    }
    ledTime = millis();
  }

  	/* 	
		Apaga todas las resistencias debido a que el maestro no ha enviado un dato
		durante 5 segundos. Hubo un fallo en el maestro.
	*/
  if (millis() - protectionTime >= 5000) {
    for (uint8_t i = 0; i < NUMBER_OF_HEATING_RESISTORS; ++i) {
      digitalWrite(HEATING_RESISTORS_PINS[i], LOW);
    }
    currentStatus = STOP;
  }

  // Reinicia el Watchdog Timer 
  wdt_reset();
}

/*	
	Rutina de servicio de interrupción para SPI
	Primeramente se verifica y el maestro quiere comenzar la transacción
	de los valores de semiciclos para cada resistencia. Posteriomente, se 
	guarda cada valor recibido uno a uno.
*/
ISR(SPI_STC_vect) {
  dataReceived = SPDR;
  if (dataReceived == START_TRANSACTION) {
    currentStatus = IN_TRANSACTION;
    index = 0;
  } else if (currentStatus == IN_TRANSACTION) {
	/*	
		En caso de que se haya recibido un valor mayor al máximo valor
		de semiciclos permitidos se tomará como erróneo y se guardará en
		su lugar 0.
	*/
    if (dataReceived > MAX_NUMBER_OF_SEMICYCLES) {
      semicyclesPerHeatingResistor[index] = 0;
    } else {
      semicyclesPerHeatingResistor[index] = dataReceived;
    }
    ++index;
    if (index >= NUMBER_OF_HEATING_RESISTORS) {
      index = 0;
      currentStatus = WAITING;
    }
  }

  // Actualizar el tiempo de protección
  protectionTime = millis();
}

// Función para actualizar las salidas basadas en el cruce por cero
void updateOutputs() {
  // Actualizar el contador de semiciclos
  if (currentStatus != STOP) {
    ++semicyclesCounter;

    // Controlar las salidas en función de los semiciclos
    for (uint8_t i = 0; i < NUMBER_OF_HEATING_RESISTORS; ++i) {
      if (semicyclesCounter <= semicyclesPerHeatingResistor[i]) {
        digitalWrite(HEATING_RESISTORS_PINS[i], HIGH);
      } else {
        digitalWrite(HEATING_RESISTORS_PINS[i], LOW);
      }
    }

    // Reiniciar el contador de semiciclos cuando llega al máximo permitido
    if (semicyclesCounter > MAX_NUMBER_OF_SEMICYCLES) {
      semicyclesCounter = 0;
    }
  }
}
