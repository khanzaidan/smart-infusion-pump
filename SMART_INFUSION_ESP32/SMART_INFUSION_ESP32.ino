/* ============================================
 * SMART INFUSION PUMP
 * ESP32 WiFi Communication Firmware
 * Bridges STM32 UART to WiFi Web Dashboard
 * ============================================ */

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

/* WiFi credentials - change these */
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

/* UART pins to STM32 */
#define STM32_RX_PIN    34
#define STM32_TX_PIN    35

/* Web server on port 80 */
WebServer server(80);

/* Latest data from STM32 */
String latestJson = "{\"t\":0,\"f\":0,\"v\":0,\"b\":0,\"a\":0}";

/* HTML dashboard */
const char* htmlPage =
"<!DOCTYPE html><html><head>"
"<title>Smart Infusion Pump</title>"
"<meta name='viewport' content='width=device-width'>"
"<meta http-equiv='refresh' content='3'>"
"<style>"
"body{font-family:Arial;background:#1a1a2e;"
"color:white;text-align:center;padding:20px;}"
".card{background:#16213e;border-radius:10px;"
"padding:20px;margin:10px;display:inline-block;"
"min-width:180px;}"
".value{font-size:2em;color:#00d4ff;"
"font-weight:bold;}"
".label{color:#aaa;font-size:0.9em;"
"margin-top:5px;}"
".alarm{background:#ff4444;padding:10px;"
"border-radius:5px;margin:10px;}"
".ok{background:#00aa44;padding:10px;"
"border-radius:5px;margin:10px;}"
"button{background:#00d4ff;border:none;"
"padding:12px 25px;margin:5px;"
"border-radius:5px;cursor:pointer;"
"font-size:1em;font-weight:bold;}"
"button:hover{background:#00aacc;}"
"h1{color:#00d4ff;}"
"</style></head><body>"
"<h1>Smart Infusion Pump</h1>"
"<h3>Walnut Medical Embedded R&D</h3>"
"<div id='cards'>Loading...</div>"
"<div id='status'></div>"
"<div style='margin:20px'>"
"<button onclick=\"sendCmd('start')\">"
"START</button>"
"<button onclick=\"sendCmd('stop')\">"
"STOP</button>"
"<button onclick=\"sendCmd('pause')\">"
"PAUSE</button>"
"</div>"
"<p id='ip' style='color:#555;font-size:0.8em'></p>"
"<script>"
"document.getElementById('ip').innerText="
"'Device IP: '+location.hostname;"
"function sendCmd(c){"
"fetch('/cmd?c='+c)"
".then(r=>r.text())"
".then(t=>console.log(t));}"
"function update(){"
"fetch('/data')"
".then(r=>r.json())"
".then(d=>{"
"document.getElementById('cards').innerHTML="
"'<div class=card>'"
"+'<div class=value>'+d.f.toFixed(1)+'</div>'"
"+'<div class=label>mL / hr</div></div>'"
"+'<div class=card>'"
"+'<div class=value>'+d.v.toFixed(2)+'</div>'"
"+'<div class=label>mL Infused</div></div>'"
"+'<div class=card>'"
"+'<div class=value>'+d.t.toFixed(1)+'</div>'"
"+'<div class=label>Temperature C</div></div>'"
"+'<div class=card>'"
"+'<div class=value>'+d.b+'%</div>'"
"+'<div class=label>Battery</div></div>';"
"var s=document.getElementById('status');"
"if(d.a==0){"
"s.innerHTML='<div class=ok>System OK</div>';}"
"else if(d.a==1){"
"s.innerHTML='<div class=alarm>ALARM: BUBBLE DETECTED</div>';}"
"else if(d.a==2){"
"s.innerHTML='<div class=alarm>ALARM: OCCLUSION</div>';}"
"else if(d.a==3){"
"s.innerHTML='<div class=alarm>ALARM: LIMIT HIT</div>';}"
"else if(d.a==4){"
"s.innerHTML='<div class=alarm>ALARM: BATTERY LOW</div>';}"
"else if(d.a==5){"
"s.innerHTML='<div class=alarm>ALARM: TEMP HIGH</div>';}"
"else{"
"s.innerHTML='<div class=alarm>ALARM: '+d.a+'</div>';}"
"}).catch(e=>console.log(e));}"
"setInterval(update,2000);"
"update();"
"</script></body></html>";

/* Handle root page */
void handleRoot()
{
    server.send(200, "text/html", htmlPage);
}

/* Handle data request */
void handleData()
{
    server.send(200,
        "application/json", latestJson);
}

/* Handle command from dashboard */
void handleCommand()
{
    if(server.hasArg("c"))
    {
        String cmd = server.arg("c");

        /* Build JSON command for STM32 */
        String msg = "{\"cmd\":\"" +
            cmd + "\"}\r\n";

        /* Send to STM32 via UART */
        Serial2.print(msg);

        Serial.print("Command sent: ");
        Serial.println(msg);

        server.send(200,
            "text/plain", "OK");
    }
    else
    {
        server.send(400,
            "text/plain", "Missing command");
    }
}

/* Handle 404 */
void handleNotFound()
{
    server.send(404,
        "text/plain", "Not found");
}

void setup()
{
    /* Debug serial */
    Serial.begin(115200);
    Serial.println("\nSmart Infusion ESP32 Starting");

    /* UART2 to STM32 */
    Serial2.begin(115200,
        SERIAL_8N1,
        STM32_RX_PIN,
        STM32_TX_PIN);

    Serial.println("UART2 to STM32 ready");

    /* Connect to WiFi */
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while(WiFi.status() != WL_CONNECTED
        && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if(WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        /* Start Access Point if WiFi fails */
        Serial.println(
            "\nWiFi failed. Starting AP mode");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("SmartInfusion_AP",
            "infusion123");
        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());
        Serial.println(
            "Connect to SmartInfusion_AP");
        Serial.println(
            "Password: infusion123");
    }

    /* Register web server routes */
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/cmd", handleCommand);
    server.onNotFound(handleNotFound);

    /* Start server */
    server.begin();
    Serial.println("Web server started");
    Serial.println("Open browser and navigate to IP");
}

void loop()
{
    /* Handle incoming web requests */
    server.handleClient();

    /* Read JSON data from STM32 */
    if(Serial2.available())
    {
        String incoming =
            Serial2.readStringUntil('\n');
        incoming.trim();

        /* Validate it looks like JSON */
        if(incoming.length() > 0 &&
           incoming.startsWith("{") &&
           incoming.endsWith("}"))
        {
            latestJson = incoming;

            /* Debug print */
            Serial.print("STM32 data: ");
            Serial.println(latestJson);
        }
    }

    /* Small delay to prevent watchdog */
    delay(1);
}