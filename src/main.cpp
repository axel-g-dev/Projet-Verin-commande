#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>


// --------------------------------------------
// WIFI
// --------------------------------------------
const char* ssid = "raf";
const char* password = "Motdepasse12.";
const char* nomHoteESP32 ="ESP32_BTSCIEL";

AsyncWebServer server(80);

// --------------------------------------------
// ADS1115
// --------------------------------------------
Adafruit_ADS1115 ads;
float Voltage = 0.0;
float position_actuelle = 0.0;

// --------------------------------------------
// MOTEUR L298N
// --------------------------------------------
int motor1Pin1 = 17;
int motor1Pin2 = 16;
int enable1Pin = 18;

// PWM
const int freq = 30000;
const int pwmChannel = 0;
const int resolution = 8;

// --------------------------------------------
// VARIABLES DE COMMANDE
// --------------------------------------------
float consigne = 26.0;
const float POSITION_MIN = 6.0;   // Limite basse en cm
const float POSITION_MAX = 36.0;  // Limite haute en cm

unsigned long ancien_affichage = 0;

// --------------------------------------------
// MOYENNE GLISSANTE
// --------------------------------------------
const int taille_tableau = 10;
float tab_mesure[taille_tableau];
int index_tab = 0;
bool tableau_rempli = false;

// --------------------------------------------
// FONCTIONS MOYENNE GLISSANTE
// --------------------------------------------
void ajouterMesure(float mesure) {
    tab_mesure[index_tab] = mesure;
    index_tab++;
    
    if (index_tab >= taille_tableau) {
        tableau_rempli = true;
        index_tab = 0;
    }
}

float calculerMoyenne() {
    float somme = 0;
    int nb_elements = tableau_rempli ? taille_tableau : index_tab;
    
    if (nb_elements == 0) return 0;
    
    for (int i = 0; i < nb_elements; i++) {
        somme += tab_mesure[i];
    }
    
    return somme / nb_elements;
}

// --------------------------------------------
// SETUP
// --------------------------------------------
void setup(void)
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== DEMARRAGE ESP32 ===");

  // --- GPIO MOTEUR ---
  pinMode(motor1Pin1, OUTPUT);
  pinMode(motor1Pin2, OUTPUT);
  pinMode(enable1Pin, OUTPUT);

  // Arrêt moteur au démarrage
  digitalWrite(motor1Pin1, LOW);
  digitalWrite(motor1Pin2, LOW);
  digitalWrite(enable1Pin, LOW);

  ledcSetup(pwmChannel, freq, resolution);
  ledcAttachPin(enable1Pin, pwmChannel);
  ledcWrite(pwmChannel, 0);

  Serial.println("Moteur initialisé");

  // --- SPIFFS ---
  if (!SPIFFS.begin(true))
  {
    Serial.println("ERREUR: Impossible de monter SPIFFS");
    return;
  }
  Serial.println("SPIFFS monté avec succès");

  // --- WIFI ---
  WiFi.setHostname(nomHoteESP32);
  WiFi.begin(ssid, password);
  Serial.print("Connexion WiFi");
  
  int tentatives = 0;
  while (WiFi.status() != WL_CONNECTED && tentatives < 20)
  {
    delay(500);
    Serial.print(".");
    tentatives++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connecté !");
    Serial.print("IP : ");
    Serial.println(WiFi.localIP());
    Serial.print("Hostname : ");
    Serial.println(nomHoteESP32);
  } else {
    Serial.println("\nErreur connexion WiFi - Mode AP non activé");
  }

  // --------------------------------------------
  // SERVEUR WEB (SPIFFS)
  // --------------------------------------------

  // Page principale
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(SPIFFS, "/index.html", "text/html");
  });

  // CSS
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(SPIFFS, "/style.css", "text/css");
  });

  // JavaScript
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(SPIFFS, "/script.js", "text/javascript");
  });

  // Image logo (si présente)
  server.on("/carnus.png", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(SPIFFS, "/carnus.png", "image/png");
  });

  // Réception consigne avec validation
  server.on("/setConsigne", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (request->hasArg("c")) {
      float nouvelle_consigne = request->arg("c").toFloat();
      
      // Validation des limites
      if (nouvelle_consigne >= POSITION_MIN && nouvelle_consigne <= POSITION_MAX) {
        consigne = nouvelle_consigne;
        Serial.print("Nouvelle consigne : ");
        Serial.print(consigne);
        Serial.println(" cm");
        request->send(200, "text/plain", "OK");
      } else {
        Serial.print("Consigne refusée (hors limites) : ");
        Serial.println(nouvelle_consigne);
        request->send(400, "text/plain", "Erreur: Consigne hors limites [6-36cm]");
      }
    } else {
      request->send(400, "text/plain", "Erreur: Paramètre manquant");
    }
  });

  // Envoi position actuelle
  server.on("/position", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", String(position_actuelle, 1));
  });

  // Gestion 404
  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Page non trouvée");
  });

  server.begin();
  Serial.println("Serveur web actif sur le port 80");

  // --- ADS1115 ---
  if (!ads.begin(0x48))
  {
    Serial.println("ERREUR: ADS1115 non détecté sur I2C 0x48");
    while (1) {
      delay(1000);
      Serial.println("Vérifiez les connexions I2C...");
    }
  }
  ads.setGain(GAIN_ONE);
  Serial.println("ADS1115 initialisé");
  
  Serial.println("\n=== SYSTEME PRET ===\n");
}

// --------------------------------------------
// LOOP
// --------------------------------------------
void loop(void)
{
  // Lecture capteur
  int16_t numCapteur = ads.readADC_SingleEnded(0);

  Voltage = (numCapteur * 4.096) / 32767.0;

  // Calcul position avec polynôme
  float position_brute = 62.9
    - 0.0145 * numCapteur
    + 1.65e-06 * pow(numCapteur, 2)
    - 9.32e-11 * pow(numCapteur, 3)
    + 2.05e-15 * pow(numCapteur, 4);

  // Application moyenne glissante
  ajouterMesure(position_brute);
  position_actuelle = calculerMoyenne();

  // --- CONTROLE MOTEUR ---
  float bande_proportionnelle = 3.0;
  float trigger = 0.4;
  int pwm_max = 255;
  int pwm_min = 160;
  float ordre_pwm;

  float erreur = consigne - position_actuelle;

  // Détermination du sens
  if (erreur > trigger)
  {
    digitalWrite(motor1Pin1, LOW);
    digitalWrite(motor1Pin2, HIGH);
  }
  else if (erreur < -trigger)
  {
    digitalWrite(motor1Pin1, HIGH);
    digitalWrite(motor1Pin2, LOW);
  }
  else
  {
    digitalWrite(motor1Pin1, LOW);
    digitalWrite(motor1Pin2, LOW);
  }

  // Calcul PWM proportionnel
  erreur = abs(erreur);

  if (erreur > bande_proportionnelle + trigger)
    ordre_pwm = pwm_max;
  else if (erreur > trigger)
    ordre_pwm = pwm_min + ((erreur - trigger) / bande_proportionnelle) * (pwm_max - pwm_min);
  else
    ordre_pwm = 0;

  ledcWrite(pwmChannel, int(ordre_pwm));

  // --- AFFICHAGE SERIE ---
  if (millis() > ancien_affichage + 200)
  {
    Serial.print("Position : ");
    Serial.print(position_actuelle, 1);
    Serial.print(" cm | Consigne : ");
    Serial.print(consigne, 1);
    Serial.print(" cm | Erreur : ");
    Serial.print(consigne - position_actuelle, 2);
    Serial.print(" cm | PWM : ");
    Serial.println(int(ordre_pwm));

    ancien_affichage = millis();
  }
}