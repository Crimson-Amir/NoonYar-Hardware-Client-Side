#ifndef TASKS_H
#define TASKS_H

#include "config.h"
#include "types.h"

// ---------- TASK FUNCTIONS ----------
void fetchInitTask(void* param);
void newCustomerTask(void* param);
void nextTicketTask(void* param);
void currentTicketTask(void* param);
void skipTicketTask(void* param);
void ticketFlowTask(void* param);
void scannerTask(void* param);


#endif
