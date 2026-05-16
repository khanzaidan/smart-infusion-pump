/* ============================================
 * SMART INFUSION PUMP
 * ATmega328P Safety Watchdog Firmware
 * Controls: HALL, LIM1, LIM2, BUBL_DIG
 * Communicates with STM32 via UART3
 * ============================================ */

/* Pin definitions matching schematic sheet 2 */
#define RELAY_CTRL_PIN    8   /* PB0 */
#define HALL_PIN          A4  /* PC4 */
#define LIM1_PIN          9   /* PB1 */
#define LIM2_PIN          10  /* PB2 */
#define BUBL_DIG_PIN      2   /* PD2 */
#define WDI_PIN           3   /* PD3 */

/* Timing constants */
#define HEARTBEAT_TIMEOUT 2000  /* ms */
#define WDI_KICK_INTERVAL 500   /* ms */
#define DEBOUNCE_MS       20    /* ms */

/* System state */
unsigned long lastHeartbeat = 0;
unsigned long lastWdiKick = 0;
unsigned long lastHallTime = 0;
bool systemSafe = true;
bool motorEnabled = false;
uint32_t dropCount = 0;
uint32_t totalDrops = 0;

/* Alarm flags */
bool alarmBubble = false;
bool alarmLim1 = false;
bool alarmLim2 = false;
bool alarmWatchdog = false;

void setup()
{
    /* UART to STM32 at 9600 baud */
    Serial.begin(9600);

    /* Configure pins */
    pinMode(RELAY_CTRL_PIN, OUTPUT);
    pinMode(HALL_PIN, INPUT_PULLUP);
    pinMode(LIM1_PIN, INPUT_PULLUP);
    pinMode(LIM2_PIN, INPUT_PULLUP);
    pinMode(BUBL_DIG_PIN, INPUT);
    pinMode(WDI_PIN, OUTPUT);

    /* Enable relay on startup */
    digitalWrite(RELAY_CTRL_PIN, HIGH);

    /* Start WDI low */
    digitalWrite(WDI_PIN, LOW);

    lastHeartbeat = millis();
    lastWdiKick = millis();

    /* Send ready message */
    Serial.println("ATM_READY");
}

void loop()
{
    unsigned long now = millis();

    /* ---- Kick WDI every 500ms ---- */
    if(now - lastWdiKick >= WDI_KICK_INTERVAL)
    {
        digitalWrite(WDI_PIN,
            !digitalRead(WDI_PIN));
        lastWdiKick = now;
    }

    /* ---- Check STM32 heartbeat ---- */
    if(Serial.available())
    {
        String msg = Serial.readStringUntil('\n');
        msg.trim();

        if(msg == "H")
        {
            /* Heartbeat received */
            lastHeartbeat = now;
            systemSafe = true;
            alarmWatchdog = false;
        }
        else if(msg == "START")
        {
            motorEnabled = true;
            digitalWrite(RELAY_CTRL_PIN, HIGH);
            Serial.println("RELAY_ON");
        }
        else if(msg == "STOP")
        {
            motorEnabled = false;
            digitalWrite(RELAY_CTRL_PIN, LOW);
            Serial.println("RELAY_OFF");
        }
        else if(msg == "RESET")
        {
            alarmBubble = false;
            alarmLim1 = false;
            alarmLim2 = false;
            systemSafe = true;
            if(motorEnabled)
            {
                digitalWrite(RELAY_CTRL_PIN,
                    HIGH);
            }
            Serial.println("RESET_OK");
        }
    }

    /* ---- Watchdog timeout check ---- */
    if(now - lastHeartbeat > HEARTBEAT_TIMEOUT)
    {
        systemSafe = false;
        alarmWatchdog = true;
        digitalWrite(RELAY_CTRL_PIN, LOW);
        Serial.println("WDT_TIMEOUT");
    }

    /* ---- Bubble sensor check ---- */
    if(digitalRead(BUBL_DIG_PIN) == HIGH)
    {
        if(!alarmBubble)
        {
            alarmBubble = true;
            digitalWrite(RELAY_CTRL_PIN, LOW);
            Serial.println("BUBBLE");
        }
    }

    /* ---- Limit switch 1 check ---- */
    if(digitalRead(LIM1_PIN) == LOW)
    {
        delay(DEBOUNCE_MS);
        if(digitalRead(LIM1_PIN) == LOW)
        {
            if(!alarmLim1)
            {
                alarmLim1 = true;
                digitalWrite(RELAY_CTRL_PIN,
                    LOW);
                Serial.println("LIM1_HIT");
            }
        }
    }

    /* ---- Limit switch 2 check ---- */
    if(digitalRead(LIM2_PIN) == LOW)
    {
        delay(DEBOUNCE_MS);
        if(digitalRead(LIM2_PIN) == LOW)
        {
            if(!alarmLim2)
            {
                alarmLim2 = true;
                digitalWrite(RELAY_CTRL_PIN,
                    LOW);
                Serial.println("LIM2_HIT");
            }
        }
    }

    /* ---- Hall sensor drop counting ---- */
    if(digitalRead(HALL_PIN) == LOW)
    {
        if(now - lastHallTime > 50)
        {
            dropCount++;
            totalDrops++;
            lastHallTime = now;

            /* Send drop count every 10 drops */
            if(dropCount % 10 == 0)
            {
                Serial.print("DROPS:");
                Serial.println(totalDrops);
            }
        }
    }

    /* ---- Send status every 5 seconds ---- */
    static unsigned long lastStatus = 0;
    if(now - lastStatus >= 5000)
    {
        Serial.print("STATUS:");
        Serial.print(systemSafe ? "OK" : "FAIL");
        Serial.print(",DROPS:");
        Serial.print(totalDrops);
        Serial.print(",B:");
        Serial.print(alarmBubble ? "1" : "0");
        Serial.print(",L1:");
        Serial.print(alarmLim1 ? "1" : "0");
        Serial.print(",L2:");
        Serial.println(alarmLim2 ? "1" : "0");
        lastStatus = now;
    }

    delay(10);
}