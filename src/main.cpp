#include "main.h"
#include <driver/adc.h>

//define clients
WiFiClient espClient;
PubSubClient client(espClient);

#define MoisureSens_Poll_Interval 60000  // Intervall zwischen zwei Bodenfeuchtemessungen in Millisekunden
#define MaxSensors 3
#define ADCAttenuation ADC_ATTEN_DB_11


/********************************** START SETUP WIFI*****************************************/
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
   Serial.print("MAC: ");
   Serial.println(WiFi.macAddress());
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
   // ESP.wdtFeed();
    delay(500);
    yield();
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

/********************************** START RECONNECT*****************************************/
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(SENSORNAME, mqtt_username, mqtt_password)) {
      Serial.println("connected");
      //client.subscribe(light_notify_topic);

      //ESP.wdtFeed();
      delay(500);    

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      yield();
      delay(5000);
    }
  }
}



struct MoistureSensorCalibrationData
{
  int Data[MaxSensors * 2] = {490, 2970, 732, 2313, 483, 2360}; // Calibration Data für Feuchtigkeitssensor. Bitte Projekt Text beachten und Werte ensprechend anpassen
    byte StatusBorderPercentValues[MaxSensors * 2][2] = { {10, 50},     // Zweidimensinonales Array für Prozentgrenzwerte (Ampel) jeweils Einzeln pro Feuchtesensor (1 -6)
    {10, 50},
    {10, 50}
  };
};

struct MoistureSensorData
{
  int Percent[MaxSensors] = {0,0,0};  // Feuchtigkeitssensordaten in Prozent
  byte Old_Percent[MaxSensors] = {0, 0, 0}; // Vorherige _ Feuchtigkeitssensordaten in Prozent (Zweck: DatenMenge einsparen.)
  bool DataValid [MaxSensors] = {false, false, false};
};

//Global Variables
MoistureSensorCalibrationData MCalib;
MoistureSensorData MMeasure;
byte AttachedMoistureSensors; // Detected Active Moisture Sensors (Count)
unsigned long Moisure_ServiceCall_Handler = 0;  // Delay Variable for Delay between Moisure Readings






void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  //client.setCallback(callback);
  //Serial.println("Client ready");
  DEBUG_MSG("Client ready\n");

  Serial.println("Ready");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
AttachedMoistureSensors = DetectMoistureSensors();
  Serial.println(F("Systemkonfiguration:"));
  Serial.print(AttachedMoistureSensors);
  Serial.println(F(" Bodenfeuchtigkeitsensor(en)"));


}


byte DetectMoistureSensors ()
{
#define MinSensorValue 100
  byte Detected = 0;
  for (int i = 0; i < MaxSensors; i++)
  {
    int MSensorRawValue = ReadMoistureSensorVal(i);
    if ( MSensorRawValue > MinSensorValue) {
      Detected++;
    } else {
      break;
    }
  }
  if (Detected < 1)
  {
    Serial.println(F("Keine Bodenfeuchtigkeitssesoren erkannt. System angehalten."));
    esp_deep_sleep_start();
    while (1) {}
  }
  return Detected;
}



int ReadMoistureSensorVal(byte Sensor)
{
  int ReturnValue, i;
  long sum = 0;
#define NUM_READS 6
  adc1_config_width(ADC_WIDTH_BIT_12);   //Range 0-4095

  switch (Sensor)
  {
    case 0:
      {
        adc1_config_channel_atten(ADC1_CHANNEL_0, ADCAttenuation);
        for (i = 0; i < NUM_READS; i++) { // Averaging algorithm
          sum += adc1_get_raw( ADC1_CHANNEL_0 ); //Read analog
        }
        ReturnValue = sum / NUM_READS;
        break;
      }
      case 1:
      {
        adc1_config_channel_atten(ADC1_CHANNEL_4, ADCAttenuation);
        for (i = 0; i < NUM_READS; i++) { // Averaging algorithm
          sum += adc1_get_raw( ADC1_CHANNEL_3 ); //Read analog
        }
        ReturnValue = sum / NUM_READS;
        break;
      }
    case 2:
      {
        adc1_config_channel_atten(ADC1_CHANNEL_6, ADCAttenuation);
        for (i = 0; i < NUM_READS; i++) { // Averaging algorithm
          sum += adc1_get_raw( ADC1_CHANNEL_6 ); //Read analog
        }
        ReturnValue = sum / NUM_READS;
        break;
      }
       default:
      {
        adc1_config_channel_atten(ADC1_CHANNEL_5, ADCAttenuation);
        for (i = 0; i < NUM_READS; i++) { // Averaging algorithm
          sum += adc1_get_raw( ADC1_CHANNEL_5 ); //Read analog
        }
        ReturnValue = sum / NUM_READS;
        break;
      }
  }
  return ReturnValue;
}




void loop() {
   //reconnect
  if (!client.connected()) {
    reconnect();
  }
  //check Wifi state
  if (WiFi.status() != WL_CONNECTED) {
    delay(1);
    yield();
    Serial.print("WIFI Disconnected. Attempting reconnection.");
    setup_wifi();
    return;
  }
  //ESP.wdtFeed();
  client.loop();

Run_MoistureSensors(false);
}


void Run_MoistureSensors (bool Init)   // HauptFunktion zum Betrieb der Bodenfeuchtesensoren
{
  byte MinSensValue = 100;
  if ((millis() - Moisure_ServiceCall_Handler >= MoisureSens_Poll_Interval) || (Init))
  {
    
    Get_Moisture_DatainPercent();
    DynamicJsonDocument jsonDoc(BUFFER_SIZE);
    jsonDoc.clear();
    ;
    for (int i = 0; i < AttachedMoistureSensors; i++)
    {
      if (MMeasure.DataValid[i])
      {
       // if (MMeasure.Percent[i] != MMeasure.Old_Percent[i])
       // {
          MMeasure.Old_Percent[i] = MMeasure.Percent[i];
          if (MMeasure.Percent[i] < MinSensValue ) {
            MinSensValue = MMeasure.Percent[i];
          };
          Serial.print(F("Feuchtigkeitswert Sensor "));
          Serial.print(i);
          Serial.print(F(" in Prozent :"));
          Serial.print(MMeasure.Percent[i]);
          Serial.println(F(" %"));
          //String num = (String) i;
          jsonDoc["Plant" +(String) i] = MMeasure.Percent[i];
          Serial.print("JSON: ");
          serializeJson(jsonDoc, Serial);
          Serial.println(" ");
       
        //}
      } else
      {
   
        Serial.print(F("Sensor "));
        Serial.print(i);
        Serial.print(F(" nicht kalibiert. Bitte kalibrieren. Rohdatenwert:"));
        Serial.println(MMeasure.Percent[i]);

        //Serial.println("Plant %d", 12);
        jsonDoc["Plant"+ (String) i] =-1;
        Serial.print("JSON: ");
          serializeJson(jsonDoc, Serial);
          Serial.println(" ");
      }
      //delay(5000);
     
    }
    
      sendToMqtt(jsonDoc,plant_topic);
      Moisure_ServiceCall_Handler = millis();
    
     
  }
}

void Get_Moisture_DatainPercent()
{
  byte CalibDataOffset = 0;
  for (byte i = 0; i < AttachedMoistureSensors; i++)
  {
    CalibDataOffset =  i * 2;
    int RawMoistureValue = ReadMoistureSensorVal(i);

    if ((MCalib.Data[CalibDataOffset] == 0) || (MCalib.Data[CalibDataOffset + 1] == 0)) // MinADC Value maxADC ADC Value
    {
      MMeasure.Percent[i] = RawMoistureValue;
      MMeasure.DataValid[i] = false;
    } else
    {
      RawMoistureValue = MCalib.Data[CalibDataOffset + 1] - RawMoistureValue;
      RawMoistureValue = MCalib.Data[CalibDataOffset] + RawMoistureValue;
      MMeasure.Percent[i] = map(RawMoistureValue, MCalib.Data[CalibDataOffset], MCalib.Data[CalibDataOffset + 1], 0, 100);
      if ((MMeasure.Percent[i] > 100 ) | (MMeasure.Percent[i] < 0 ))
      {
        MMeasure.Percent[i] = RawMoistureValue;
        MMeasure.DataValid[i] = false;
      } else  {
        MMeasure.DataValid[i] = true;
      }
    }
  }
  return ;
}


/**********************************Send to MQTT ***********************************************/
void sendToMqtt(String path, bool val, const char* destinationTopic){
  StaticJsonDocument<BUFFER_SIZE> jsonDoc;
  Serial.print("Sending Mesage: ");
  Serial.print(path);
  Serial.print(" -> ");
  Serial.println(val);

  jsonDoc[path] = val;

  char buffer[BUFFER_SIZE];
  serializeJson(jsonDoc, buffer);
  yield();

  client.publish(destinationTopic, buffer, true);
  yield();
  Serial.println("Message sent");
}

void sendToMqtt(DynamicJsonDocument jsonDoc, const char* destinationTopic){
   Serial.print("Sending Mesage: ");
   serializeJson(jsonDoc, Serial);
   Serial.println("");
   yield();
   char buffer[BUFFER_SIZE];
   serializeJson(jsonDoc, buffer);
   yield();
   client.publish(destinationTopic, buffer,true);
   yield();
}