#include <stdio.h>
#include <stdio.h>
#include "doomgeneric.h"

// global vars for network/logic
int drone = 0;
int net_client_connected = 0;

// window title (js print it for now)
void DG_SetWindowTitle(const char * title) {
    printf("doom window: %s\n", title);
}

void DG_Init() {}
void DG_DrawFrame() {}
uint32_t DG_GetTicksMs() {return 0;}
void DG_SleepMs(uint32_t ms) {}
int DG_GetKey(int* pressed, unsigned char* key) {return 0;}

void app_main(void) {

    doomgeneric_Create(0, NULL);
    while (1) {doomgeneric_Tick();}

}
