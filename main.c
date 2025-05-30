#include <stdio.h>
#include "rf.h"

#define N_FEAT  13      //  <-- put your real feature count
#define N_CLASS 3       //  <-- put your real class count

void app_main(void)
{
    double x[N_FEAT]   = {0};  // demo inputs – replace with sensor data
    double probs[N_CLASS];

    score(x, probs);           // call the model
    int cls = 0;
    for (int i = 1; i < N_CLASS; ++i)
        if (probs[i] > probs[cls]) cls = i;

    printf("Predicted class: %d\n", cls);
}
