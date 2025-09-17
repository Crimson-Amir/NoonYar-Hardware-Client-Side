#ifndef API_H
#define API_H

#include "config.h"
#include "types.h"

// ---------- GLOBAL DATA ----------
extern volatile bool init_success;
extern const char* endpoint_address;

// ---------- API FUNCTIONS ----------
bool fetchInitData();
int apiNewCustomer(const std::vector<int>& breads);
NextTicketResponse apiNextTicket(int customer_ticket_id);
CurrentTicketResponse apiCurrentTicket();
bool apiSkipTicket(int customer_ticket_id);
bool isTicketInSkippedList(int customer_ticket_id);
// ---------- STORAGE FUNCTIONS ----------
void saveInitDataToFlash();

#endif
