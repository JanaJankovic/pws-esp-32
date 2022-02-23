#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Recipient.h>
#include <DHT.h>

#define LDR_PIN 34
#define DHT11PIN 16
#define ECHO 22
#define TRIGG 23
#define RELAY1 25
#define RELAY2 26
#define RELAY3 27
// 32, 33, 35, 36, 39 are ok for reading moisture

const char* ssid     = "xxxxxx";
const char* password = "xxxxxx";

const char* userUrl = "https://pws-server.herokuapp.com/recipients/short/618305880b64dd2ff247062a/0000";
const char* requestUrl = "https://pws-server.herokuapp.com/requests/618305880b64dd2ff247062a/0000";
const char* responseUrl = "https://pws-server.herokuapp.com/responses/";
const char* graphDataUrl = "https://pws-server.herokuapp.com/graph_data/";
const char* notificationsUrl = "https://pws-server.herokuapp.com/users/notification/618305880b64dd2ff247062a";

const int DT_SIZE = JSON_OBJECT_SIZE(15);
const int R_SIZE = JSON_ARRAY_SIZE(3) + 3 * JSON_OBJECT_SIZE(10);
const int RQ_SIZE = JSON_OBJECT_SIZE(10);
const int RS_SIZE = JSON_OBJECT_SIZE(8);
const int NTF_SIZE = JSON_OBJECT_SIZE(3);
const int W_SIZE = JSON_OBJECT_SIZE(1);
const int GD_SIZE = JSON_OBJECT_SIZE(5);

const long int loopsLimit = 2500;

//response
String requestId;
String recipientGDId;
String lastPumpRequest;
int l;
int h;
int t;
int m;
char* message;

//notification
String jsonNotification;

int rqCode = -1;

int c = 0;
int checker = 0;
int dbUpdater = 0;
Recipient* recipients[3];

DHT dht(DHT11PIN, DHT11);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  dht.begin();
  pinMode(ECHO, INPUT);
  pinMode(TRIGG, OUTPUT);
  pinMode(RELAY1, OUTPUT);
  digitalWrite(RELAY1, HIGH);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY2, HIGH);
  pinMode(RELAY3, OUTPUT);
  digitalWrite(RELAY3, HIGH);
}

void loop() {
  //testCircuitry();
  recipientProtocol();
  requestProtocol();
  tendingProtocol();

}

void testCircuitry() {
  testRelays();

  fetchSensoryData(35);
  delay(1000);
  fetchSensoryData(36);
  delay(1000);
  fetchSensoryData(39);
  delay(1000);
}

void testRelays() {
  digitalWrite(RELAY1,  LOW);
  delay(1500);
  digitalWrite(RELAY1, HIGH);
  delay(1500);

  digitalWrite(RELAY2, LOW);
  delay(2000);
  digitalWrite(RELAY2, HIGH);
  delay(1500);

  digitalWrite(RELAY3, LOW);
  delay(1500);
  digitalWrite(RELAY3, HIGH);
  delay(1500);
}

void recipientProtocol() {
  Serial.println("Getting recipients...");
  getDataFromUrl(userUrl, 0);
  delay(1000);
}

void requestProtocol() {
  Serial.println("Getting requests...");
  getDataFromUrl(requestUrl, 1);
  delay(1000);

  Serial.println("Posting requests...");
  postDataToUrl(responseUrl, rqCode);
  delay(1000);
}


void tendingProtocol() {
  Serial.println("Tending garden...");
  for (int i = 0; i < c; i++) {
    if (recipients[i]->isWater()) {
      waterRecipient(recipients[i]->getId(), recipients[i]->getMoisturePin(), recipients[i]->getMoisture(), recipients[i]->getRelayPin());
      delay(1000);
    } else {
      Serial.print("Index ");
      Serial.print(i);
      Serial.println(" recipient doesnt need watering");
      Serial.println();
      delay(1000);
    }
    fetchSensoryData(recipients[i]->getMoisturePin());
    if (checker == loopsLimit * 2) {
      checkRequirements(recipients[i]);
      delay(1000);
      checker = 0;
    }
    if (dbUpdater == loopsLimit) {
      Serial.println("Updating db...");
      fetchSensoryData(recipients[i]->getMoisturePin());
      recipientGDId = recipients[i]->getId();
      postDataToUrl(graphDataUrl, 4);
    }
  }
  checker++;
  if (dbUpdater == loopsLimit)
    dbUpdater = 0;
  else
    dbUpdater++;
}


void  getDataFromUrl(const char* url, int code) {
  if ((WiFi.status() == WL_CONNECTED)) {

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    Serial.println(httpCode);

    if (httpCode == 200) {
      String jsons = http.getString();
      int str_len = jsons.length() + 1;
      char jsonc[str_len];
      jsons.toCharArray(jsonc, str_len);

      switch (code) {
        case 0: {
            StaticJsonDocument<R_SIZE> doc;
            DeserializationError error = deserializeJson(doc, jsonc);

            if (error) {
              Serial.print("deserializeJson() recipients failed: ");
              Serial.println(error.f_str());
              for (int i = 0; i < c; i++) {
                delete recipients[i];
              }
            } else {
              for (int i = 0; i < c; i++) {
                delete recipients[i];
              }
              c = 0;
              JsonObject r = doc[c];
              while (!r.isNull()) {
                Recipient* rec = new Recipient(r["_id"], r["byte_address"], r["moisture_pin"], r["relay_pin"], r["plant"], r["light"],
                                               r["humidity"], r["temperature"], r["moisture"], r["water"]);
                recipients[c] = rec;
                Serial.print(recipients[c]->toString());
                Serial.println();
                c++;
                r = doc[c];
              }
            }
          }
          break;
        case 1: {
            StaticJsonDocument<RQ_SIZE> doc;
            DeserializationError error = deserializeJson(doc, jsonc);

            if (error) {
              Serial.print("deserializeJson() request failed: ");
              Serial.println(error.f_str());
              rqCode = -1;
            } else {
              const char* rid = doc["_id"];
              requestId = rid;
              const char* recipientId = doc["recipient_id"];
              bool fetch = doc["fetch_data"];
              bool pump = doc["activate_pump"];
              int mp = doc["moisture_pin"];
              int rp = doc["relay_pin"];

              Serial.println("Request...");
              Serial.println(jsons);
              Serial.println(mp);
              Serial.println();

              if (fetch) {
                Serial.println("Fetch request");
                rqCode = 0;
                fetchSensoryData(mp);
              } else if (pump) {
                Serial.println("Pump request");
                rqCode = 1;
                waterRecipient(recipientId, mp, 0, rp);
              }
            }
          }
          break;
      }
    } else {
      Serial.println("Error occurred while sending HTTP GET");
    }

    http.end();
  }
}

void postDataToUrl(const char* url, int code) {
  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String requestBody = "";

    switch (code) {
      //fetch
      case 0: {
          if (requestId == "") {
            requestBody = "";
          } else {
            StaticJsonDocument<RS_SIZE> doc;
            doc["request_id"] = requestId;
            doc["light"] = l;
            doc["humidity"] = h;
            doc["temperature"] = t;
            doc["moisture"] = m;
            doc["message"] = "Fetched successfully";
            serializeJson(doc, requestBody);
            requestId = "";
          }
        }
        break;
      //pump
      case 1: {
          if (requestId == "") {
            requestBody = "";
          } else {
            StaticJsonDocument<RS_SIZE> doc;
            doc["request_id"] = requestId;
            doc["light"] = l;
            doc["humidity"] = h;
            doc["temperature"] = t;
            doc["moisture"] = m;
            doc["message"] = "Watered successfully";
            serializeJson(doc, requestBody);
            requestId = "";
          }
        }
        break;
      //notification
      case 2: {
          requestBody = jsonNotification;
          jsonNotification = "";
        }
        break;
      //recipient update, make url dynamically
      case 3: {
          StaticJsonDocument<W_SIZE> doc;
          doc["message"] = message;
          message = "";
          serializeJson(doc, requestBody);
        }
        break;
      case 4: {
          requestBody = "{\"recipient_id\":\"" + recipientGDId +
                        "\",\"light\":\"" + l + "\",\"humidity\":\"" + h +
                        "\",\"temp\":\"" + t + "\",\"moisture\":\"" + m + "\"}";
        }
        break;
    }


    if (requestBody != "") {
      Serial.println("POST: body");
      Serial.println(requestBody);
      Serial.println();

      int httpResponseCode = http.POST(requestBody);

      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println(httpResponseCode);
        Serial.println(response);
      }
      else {
        Serial.println("Error occurred while sending HTTP POST");
      }
    } else
      Serial.println("Not posting");
  }
  Serial.println();
}

float measureWaterLevel() {
  digitalWrite(TRIGG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGG, LOW);

  // measure duration of pulse from ECHO pin
  float duration_us = pulseIn(ECHO, HIGH);

  // calculate the distance
  float distance_cm = 0.017 * duration_us;

  // print the value to Serial Monitor
  Serial.print("Water level: ");
  Serial.print(distance_cm);
  Serial.println(" cm");

  return distance_cm;
}

void waterRecipient(String id, int mp, int m, int rp) {
  bool post = false;
  if (rp != -1) {
    if (measureWaterLevel() > 19.00) {
      if (m == 0)
        requestId = "";
      Serial.println("Not enough water");
      String title = "Out of water";
      String type = "Warning";
      String mssg = "Container on address 0000 is running out of water";
      jsonNotification = "{\"title\":\"" + title + "\",\"type\":\"" + type + "\",\"note\":\"" + mssg + "\"}";
      postDataToUrl(notificationsUrl, 2);
      return;
    }

    Serial.println("Watering ... ");
    if ((100.00 - ((analogRead(mp) / 4095.00) * 100.00 )) <= m - 20 || m == 0) {
      digitalWrite(rp, LOW);
      delay(1500);
      digitalWrite(rp, HIGH);
      delay(1500);
      post = true;
    }

    delay(3000);

    if (m > 0 && (100.00 - ((analogRead(mp) / 4095.00) * 100.00 )) <= m - 20) {
      Serial.println("Battery ran out...");
      Serial.println();
      jsonNotification = "{\"title\":\"Pump battery ran out or pump broke\",\"type\":\"Warning\",\"note\":\"On address 0000 and relay pin " + String(rp) + "\"}";
      postDataToUrl(notificationsUrl, 2);
      return;

    }


    if (post) {
      if (m == 0)
        message = "Request";
      else
        message = "Schedule";

      String updateUrl = "https://pws-server.herokuapp.com/recipients/water/" + id;
      char* recipientUpdateUrl;
      int len = updateUrl.length() + 1;
      char url[len];
      updateUrl.toCharArray(url, len);
      recipientUpdateUrl = url;

      Serial.print("Updating recipient on url ");
      Serial.println(recipientUpdateUrl);
      postDataToUrl(recipientUpdateUrl, 3);
    }
  }
}

void fetchSensoryData(int mp) {
  h = dht.readHumidity();
  t = dht.readTemperature();
  l = (analogRead(LDR_PIN) / 4095.00) * 100.00;
  m = (100.00 - ((analogRead(mp) / 4095.00) * 100.00 ));

  Serial.print("Humidity ");
  Serial.println(h);
  Serial.print("Temp ");
  Serial.println(t);
  Serial.print("Light % ");
  Serial.println(l);
  Serial.print("Moisture % ");
  Serial.println(m);
  Serial.println();
}

void checkRequirements(Recipient* r) {
  Serial.println("Checking plant requirements");
  bool ok = true;

  if (dht.readTemperature() < r->getTemp() - 10 || dht.readTemperature() > r->getTemp() + 10) {
    Serial.println("Bad temp");
    makeNotification("Temperature", r, false);
    postDataToUrl(notificationsUrl, 2);
    ok = false;
  }

  if (dht.readHumidity() < r->getHumidity() - 20 || dht.readHumidity() > r->getHumidity() + 20) {
    Serial.println("Bad humidity");
    makeNotification("Humidity", r, false);
    postDataToUrl(notificationsUrl, 2);
    ok = false;
  }

  if ((analogRead(LDR_PIN) / 4095.00) * 100.00 < r->getLight() - 20 ||
      (analogRead(LDR_PIN) / 4095.00) * 100.00 > r->getLight() + 20) {
    Serial.println("Bad light");
    makeNotification("Light", r, false);
    postDataToUrl(notificationsUrl, 2);
    ok = false;
  }

  if ((100.00 - ((analogRead(r->getMoisturePin()) / 4095.00) * 100.00 )) < r->getMoisture() - 20 ||
      (100.00 - ((analogRead(r->getMoisturePin()) / 4095.00) * 100.00 )) > r->getMoisture() + 20) {
    Serial.println("Bad moisture");
    makeNotification("Moisture", r, false);
    postDataToUrl(notificationsUrl, 2);
    ok = false;
  }

  if (ok) {
    Serial.println("All ok");
    makeNotification("", r, true);
  }

  postDataToUrl(notificationsUrl, 2);
}

void makeNotification(String sensor, Recipient* r, bool good) {
  Serial.println("Making notification char arrays...");
  String title;
  String type;
  String message;
  if (!good) {
    title = sensor + " not for " + r->getPlant();
    type = "Warning";
  } else {
    title = "All requirements met for " + r->getPlant();
    type = "Info";
  }
  String mssg = "On address 0000, plant on relay/moisture pin" + String(r->getRelayPin()) + "/" + String(r->getMoisturePin());
  jsonNotification = "{\"title\":\"" + title + "\",\"type\":\"" + type + "\",\"note\":\"" + mssg + "\"}";

  Serial.print("Notification ");
  Serial.println(jsonNotification);
}
