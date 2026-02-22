#include "display.h"
#include "network.h"
#include "api.h"

// ---------- DISPLAY OBJECTS ----------
DualLedControl lc(DIN_1, CLK_PIN, CS_1, DIN_2, CS_2);
volatile DeviceStatus currentStatus = STATUS_NORMAL;
int num1 = 0;
int num2 = 0;
int num3 = 0;

void displayChar(char c)
{
    lc.clearDisplay(0);
    lc.setChar(0, 0, c, false);
}

void displayDash()
{
    lc.clearDisplay(0);
    lc.setChar(0, 0, '-', false);
}

// Base pattern: show G segment (dash) on specific digits for all states
static void showBaseGPattern()
{
    // Clear both devices first
    lc.clearDisplay(0);
    lc.clearDisplay(1);

    // Device 0: digits 0,2,3,4
    lc.setChar(0, 0, '-', false);
    lc.setChar(0, 2, '-', false);
    lc.setChar(0, 1, '-', false);
    lc.setChar(0, 3, '-', false);

    // Device 1: digits 2,7
    lc.setChar(1, 0, '-', false);
    lc.setChar(1, 1, '-', false);
}

static void setConnectingDigitsDashState(bool firstReady, bool secondReady, bool thirdReady)
{
    if (firstReady)
        lc.setChar(0, 0, '-', false);
    if (secondReady)
        lc.setChar(0, 2, '-', false);
    if (thirdReady)
        lc.setChar(0, 1, '-', false);
}

static void animateConnectingDigits(uint8_t step, bool animateFirst, bool animateSecond, bool animateThird)
{
    // A -> B -> C -> D -> E -> F, then G ('-')
    const byte segmentMasks[6] = {
        0b01000000, // A
        0b00100000, // B
        0b00010000, // C
        0b00001000, // D
        0b00000100, // E
        0b00000010  // F
    };

    if (step < 6)
    {
        byte mask = segmentMasks[step];
        if (animateFirst)
            lc.setRow(0, 0, mask);
        if (animateSecond)
            lc.setRow(0, 2, mask);
        if (animateThird)
            lc.setRow(0, 1, mask);
    }
    else
    {
        if (animateFirst)
            lc.setChar(0, 0, '-', false);
        if (animateSecond)
            lc.setChar(0, 2, '-', false);
        if (animateThird)
            lc.setChar(0, 1, '-', false);
    }
}

void updateConnectionProgressDisplay()
{
    if (!(currentStatus == STATUS_WIFI_CONNECTING || currentStatus == STATUS_MQTT_CONNECTING || currentStatus == STATUS_INIT))
    {
        return;
    }

    static unsigned long lastStepAt = 0;
    static uint8_t animStep = 0;

    if (millis() - lastStepAt < 120)
    {
        return;
    }
    lastStepAt = millis();

    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool mqttConnected = mqtt.connected();
    bool ready = wifiConnected && mqttConnected && init_success;

    lc.clearDisplay(0);
    lc.clearDisplay(1);

    if (!wifiConnected)
    {
        // Stage 1: animate all three while connecting WiFi
        animateConnectingDigits(animStep, true, true, true);
    }
    else if (!mqttConnected)
    {
        // Stage 2: first is fixed '-', animate second+third while connecting MQTT
        setConnectingDigitsDashState(true, false, false);
        animateConnectingDigits(animStep, false, true, true);
    }
    else if (!ready)
    {
        // Stage 3: first+second fixed '-', animate third until fully ready
        setConnectingDigitsDashState(true, true, false);
        animateConnectingDigits(animStep, false, false, true);
    }
    else
    {
        // Stage 4: everything ready -> all three fixed '-'
        setConnectingDigitsDashState(true, true, true);
    }

    animStep = (animStep + 1) % 7;
}

// API waiting / init connecting pattern
//  - Base G pattern on specified digits
//  - Device 1: 0,1,5 -> 'C'; 3,4,6 -> '3'
static void showInitPattern()
{
    showBaseGPattern();

    lc.setChar(1, 2, 'C', false);
    lc.setChar(1, 3, 'C', false);
    lc.setChar(1, 4, 'C', false);

    lc.setChar(1, 5, '3', false);
    lc.setChar(1, 6, '3', false);
    lc.setChar(1, 7, '3', false);
}

static void showErrorCode(char codeChar)
{
    // Clear both devices so no '-' segments are shown
    lc.clearDisplay(0);
    lc.clearDisplay(1);

    // Left error column (E): 1,0 / 1,5 / 1,1
    lc.setChar(1, 2, 'E', false);
    lc.setChar(1, 3, 'E', false);
    lc.setChar(1, 4, 'E', false);

    // Right error column (code): 1,6 / 1,4 / 1,3
    lc.setChar(1, 5, codeChar, false);
    lc.setChar(1, 6, codeChar, false);
    lc.setChar(1, 7, codeChar, false);
}

void showNumbers(int a, int b, int c)
{
    if (currentStatus == STATUS_NORMAL && !confirmationMode)
    {
        // Only update the main customer digits so we don't disturb cook display on 0,4
        lc.setDigit(0, 0, a % 10, false);
        delayMicroseconds(50);
        lc.setDigit(0, 2, b % 10, false);
        delayMicroseconds(50);
        lc.setDigit(0, 1, c % 10, false);
        delayMicroseconds(50);

        // Refresh all other displays to maintain proper multiplexing
        // This prevents brightness issues when certain digit patterns are set
        showBakerDisplay();
        delayMicroseconds(100);
        showDeliveryDisplay();
        delayMicroseconds(100);
        showCookDisplay();
        delayMicroseconds(100);
    }
}

void showOwnerBreadCounts()
{
    int bakerTotal = bread1_count_baker_display + bread2_count_baker_display + bread3_count_baker_display;
    // Only show baker display when in BAKER mode and there's something to show
    if (displayMode != DISPLAY_MODE_BAKER || bakerTotal <= 0)
    {
        lc.setRow(1, 5, 0);
        delayMicroseconds(50);
        lc.setRow(1, 6, 0);
        delayMicroseconds(50);
        lc.setRow(1, 7, 0);
        return;
    }

    // Show baker-display bread counts on device 1 digits 6,4,3
    lc.setDigit(1, 5, bread1_count_baker_display % 10, false);
    delayMicroseconds(50);
    lc.setDigit(1, 6, bread2_count_baker_display % 10, false);
    delayMicroseconds(50);
    lc.setDigit(1, 7, bread3_count_baker_display % 10, false);
}

void showBakerDisplay()
{
    showOwnerBreadCounts();
}

void showDeliveryDisplay()
{
    int deliveryTotal = bread1_delivery_display + bread2_delivery_display + bread3_delivery_display;

    // Only show delivery display when in DELIVERY mode and there's something to show
    if (displayMode != DISPLAY_MODE_DELIVERY || deliveryTotal <= 0)
    {
        lc.setRow(1, 2, 0);
        delayMicroseconds(50);
        lc.setRow(1, 3, 0);
        delayMicroseconds(50);
        lc.setRow(1, 4, 0);
        return;
    }

    // Show delivery-display counts on device 1 digits 0,5,1
    lc.setDigit(1, 2, bread1_delivery_display % 10, false);
    delayMicroseconds(50);
    lc.setDigit(1, 3, bread2_delivery_display % 10, false);
    delayMicroseconds(50);
    lc.setDigit(1, 4, bread3_delivery_display % 10, false);
}

void showCookDisplay()
{
    // Show cook-display counts: 1,2 / 1,7 / 0,4
    if (bread1_cook_display <= 0 && bread2_cook_display <= 0 && bread3_cook_display <= 0)
    {
        // Nothing to show on cook display: turn digits off
        lc.setRow(1, 0, 0);
        delayMicroseconds(50);
        lc.setRow(1, 1, 0);
        delayMicroseconds(50);
        lc.setRow(0, 3, 0);
    }
    else
    {
        int c1 = bread1_cook_display > 0 ? bread1_cook_display : 0;
        int c2 = bread2_cook_display > 0 ? bread2_cook_display : 0;
        int c3 = bread3_cook_display > 0 ? bread3_cook_display : 0;
        lc.setDigit(1, 0, c1 % 10, false);
        delayMicroseconds(50);
        lc.setDigit(1, 1, c2 % 10, false);
        delayMicroseconds(50);
        lc.setDigit(0, 3, c3 % 10, false);
    }
}

void setStatus(DeviceStatus st)
{
    currentStatus = st;
    // Serial.println("New Status: " + String(st));
    lc.clearDisplay(0);
    delayMicroseconds(200);  // Allow time for clear to propagate

    if (st == STATUS_NORMAL)
    {
        showNumbers(num1, num2, num3);
        delayMicroseconds(200);
        showBakerDisplay();
        delayMicroseconds(200);
        showDeliveryDisplay();
        delayMicroseconds(200);
        showCookDisplay();
    }
    else if (st == STATUS_WIFI_CONNECTING)
    {
        updateConnectionProgressDisplay();
    }
    else if (st == STATUS_MQTT_CONNECTING)
    {
        updateConnectionProgressDisplay();
    }
    else if (st == STATUS_WIFI_ERROR)
    {
        showErrorCode('1');
    }
    else if (st == STATUS_MQTT_ERROR)
    {
        showErrorCode('2');
    }
    else if (st == STATUS_API_ERROR)
    {
        showErrorCode('3');
    }
    else if (st == STATUS_INIT || st == STATUS_API_WAITING)
    {
        if (st == STATUS_INIT)
        {
            updateConnectionProgressDisplay();
        }
        else
        {
            showInitPattern();
        }
    }
}
