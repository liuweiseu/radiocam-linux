#include "sdr_transfer.h"
#include "sdr_config.h"
#include <stdio.h>

int main () {
    int error_code = data_transfer_init();
    if (error_code != 0) {
        printf("Error with initialization.\n");
        return -1;
    }
    error_code = read_data("data_test.dat", 5);
    if (error_code != 0) {
        printf("Error with data read.\n");
        return -1;
    }
    error_code = data_transfer_close();
    if (error_code != 0) {
        printf("Error with closing data bus.\n");
        return -1;
    }
    return 0;
}



