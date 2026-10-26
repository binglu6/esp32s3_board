#include "logprint.h"

const char *TAG = "example";

void show_heap_sram_psram(void)
{
    /**
    * It's not recommended to frequently use functions like `heap_caps_get_free_size()` to obtain memory information
    * in practical applications, especially when the application extensively uses `malloc()` to dynamically allocate
    * a significant number of memory blocks. The frequent interrupt disabling may potentially lead to issues with other functionalities.
    */
    static char buffer[128];  
    sprintf(buffer, "   Biggest /     Free /    Total\n"
                    "\t  SRAM : [%8d / %8d / %8d]\n"
                    "\t PSRAM : [%8d / %8d / %8d]",
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                    heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                    heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI("MEM", "%s", buffer);
}