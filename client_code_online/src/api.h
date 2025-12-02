#ifndef API_H
#define API_H

#include "config.h"
#include "types.h"

// ---------- GLOBAL DATA ----------
extern volatile bool init_success;
extern const char* endpoint_address;
extern bool last_show_on_display;
extern String last_ticket_token;

// ---------- API FUNCTIONS ----------
bool fetchInitData();
int apiNewCustomer(const std::vector<int>& breads);
ServeTicketResponse apiServeTicket(const String& ticket_token);
CurrentTicketResponse apiCurrentTicket();
bool apiSendTicketToWaitList(int customer_ticket_id);
NewBreadResponse apiNewBread();
bool apiInitCookDisplayFromServer();
// bool isTicketInSkippedList(int customer_ticket_id);
// UpcomingCustomerResponse apiUpcomingCustomer();
// bool apiUpdateTimeout(int time_out_minute);
// ---------- STORAGE FUNCTIONS ----------
void saveInitDataToFlash();

#endif
