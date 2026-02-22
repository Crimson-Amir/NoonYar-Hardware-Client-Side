#ifndef DISPLAY_H
#define DISPLAY_H

#include <LedControl.h>
#include "config.h"
#include "types.h"

// ---------- DISPLAY OBJECTS ----------
class DualLedControl
{
public:
    DualLedControl(int din1, int clk, int cs1, int din2, int cs2)
        : lc1(din1, clk, cs1, 1), lc2(din2, clk, cs2, 1)
    {
    }

    void shutdown(int addr, bool status) { device(addr).shutdown(0, status); }
    void setIntensity(int addr, int intensity) { device(addr).setIntensity(0, intensity); }
    void clearDisplay(int addr) { device(addr).clearDisplay(0); }
    void setChar(int addr, int digit, char value, bool dp) { device(addr).setChar(0, digit, value, dp); }
    void setDigit(int addr, int digit, byte value, bool dp) { device(addr).setDigit(0, digit, value, dp); }
    void setRow(int addr, int row, byte value) { device(addr).setRow(0, row, value); }

private:
    LedControl lc1;
    LedControl lc2;

    LedControl &device(int addr) { return (addr <= 0) ? lc1 : lc2; }
};

extern DualLedControl lc;
extern volatile DeviceStatus currentStatus;
extern int num1, num2, num3;

// ---------- DISPLAY FUNCTIONS ----------
void displayChar(char c);
void displayDash();
void showNumbers(int a, int b, int c);
void setStatus(DeviceStatus st);
void showOwnerBreadCounts();
void showBakerDisplay();
void showDeliveryDisplay();
void showCookDisplay();
void updateConnectionProgressDisplay();

#endif
