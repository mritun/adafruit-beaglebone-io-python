/*
Copyright (c) 2013 Adafruit
Author: Justin Cooper

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "c_pwm.h"

#define KEYLEN 7

#define PERIOD 0
#define DUTY 1

char pwm_ctrl_dir[30];
char ocp_dir[22];
int pwm_initialized = 0;

// pwm exports
struct pwm_exp
{
    char key[KEYLEN];
    int period_fd;
    int duty_fd;
    unsigned long duty;
    unsigned long period_ns;
    struct pwm_exp *next;
};
struct pwm_exp *exported_pwms = NULL;

int load_device_tree(const char *name);

struct pwm_exp *lookup_exported_pwm(const char *key) 
{
    struct pwm_exp *pwm = exported_pwms;

    while (pwm != NULL)
    {
        if (strcmp(pwm->key, key) == 0) {
            return pwm;
        }
        pwm = pwm->next;
    }

    return 0;
}

int build_path(const char *partial_path, const char *prefix, char *full_path, size_t full_path_len)
{
    DIR *dp;
    struct dirent *ep;

    dp = opendir (partial_path);
    if (dp != NULL) {
        while ((ep = readdir (dp))) {
            if (strstr(ep->d_name, prefix)) {
                snprintf(full_path, full_path_len, "%s/%s", partial_path, ep->d_name);
                (void) closedir (dp);
                return 1;
            }
        }
        (void) closedir (dp);
    } else {
        return 0;
    }

    return 0;
}

int initialize_pwm(void)
{
    if  (build_path("/sys/devices", "bone_capemgr", pwm_ctrl_dir, sizeof(pwm_ctrl_dir)) && 
         build_path("/sys/devices", "ocp", ocp_dir, sizeof(ocp_dir))) {
        pwm_initialized = 1;

        load_device_tree("am33xx_pwm");
        return 1;
    }
    return 0;   
}

int load_device_tree(const char *name)
{
    FILE *file = NULL;
    char slots[40];
    char line[256];

    if(!pwm_initialized) {
        initialize_pwm();
    }

    snprintf(slots, sizeof(slots), "%s/slots", pwm_ctrl_dir);

    file = fopen(slots, "r+");
    if (!file) {
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        //the device is already loaded, return 1
        if (strstr(line, name)) {
            fclose(file);
            return 1;
        }
    }

    //if the device isn't already loaded, load it, and return
    fprintf(file, name);
    fclose(file);

    return 1;
}

int unload_device_tree(const char *name)
{
    FILE *file = NULL;
    char slots[40];
    char line[256];
    char *slot_line;

    snprintf(slots, sizeof(slots), "%s/slots", pwm_ctrl_dir);

    file = fopen(slots, "r+");
    if (!file) {
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        //the device is loaded, let's unload it
        if (strstr(line, name)) {
            slot_line = strtok(line, ":");
            //remove leading spaces
            while(*slot_line == ' ')
                slot_line++;

            fprintf(file, "-%s", slot_line);
            fclose(file);
            return 1;
        }
    }

    //not loaded, close file
    fclose(file);

    return 1;
}


int pwm_set_frequency(const char *key, float freq) {
    int len;
    char buffer[20];
    unsigned long period_ns;
    struct pwm_exp *pwm;

    if (freq <= 0.0)
        return -1;

    pwm = lookup_exported_pwm(key);

    if (pwm == NULL) {
        return -1;
    }

    period_ns = (unsigned long)(1e9 / freq);

    if (period_ns != pwm->period_ns) {
        pwm->period_ns = period_ns;

        len = snprintf(buffer, sizeof(buffer), "%lu", period_ns);
        write(pwm->period_fd, buffer, len);
    }

    return 1;
}

int pwm_set_duty_cycle(const char *key, float duty) {
    int len;
    char buffer[20];
    struct pwm_exp *pwm;

    if (duty < 0.0 || duty > 100.0)
        return -1;

    pwm = lookup_exported_pwm(key);

    if (pwm == NULL) {
        return -1;
    }    

    pwm->duty = (unsigned long)(pwm->period_ns * (duty / 100.0));

    len = snprintf(buffer, sizeof(buffer), "%lu", pwm->duty);
    write(pwm->duty_fd, buffer, len);

    return 0;
}

int pwm_start(const char *key, float duty, float freq)
{
    char fragment[18];
    char pwm_test_fragment[20];
    char pwm_test_path[45];
    char period_path[50];
    char duty_path[50];
    int period_fd, duty_fd;
    struct pwm_exp *new_pwm, *pwm;

    snprintf(fragment, sizeof(fragment), "bone_pwm_%s", key);
    

    if (!load_device_tree(fragment)) {
        //error enabling pin for pwm
        return -1;
    }

    //creates the fragment in order to build the pwm_test_filename, such as "pwm_test_P9_13"
    snprintf(pwm_test_fragment, sizeof(pwm_test_fragment), "pwm_test_%s", key);

    //finds and builds the pwm_test_path, as it can be variable...
    build_path(ocp_dir, pwm_test_fragment, pwm_test_path, sizeof(pwm_test_path));

    //create the path for the period and duty
    snprintf(period_path, sizeof(period_path), "%s/period", pwm_test_path);
    snprintf(duty_path, sizeof(duty_path), "%s/duty", pwm_test_path);

    //add period and duty fd to pwm list    
    if ((period_fd = open(period_path, O_RDWR)) < 0)
        return -1;


    if ((duty_fd = open(duty_path, O_RDWR)) < 0) {
        //error, close already opened period_fd.
        close(period_fd);
        return -1;
    }

    // add to list
    new_pwm = malloc(sizeof(struct pwm_exp));
    if (new_pwm == 0) {
        return -1; // out of memory
    }

    strncpy(new_pwm->key, key, KEYLEN);
    new_pwm->period_fd = period_fd;
    new_pwm->duty_fd = duty_fd;
    new_pwm->next = NULL;

    if (exported_pwms == NULL)
    {
        // create new list
        exported_pwms = new_pwm;
    } else {
        // add to end of existing list
        pwm = exported_pwms;
        while (pwm->next != NULL)
            pwm = pwm->next;
        pwm->next = new_pwm;
    }

    pwm_set_frequency(key, freq);
    pwm_set_duty_cycle(key, duty);

    return 0;
}

int pwm_disable(const char *key)
{
    struct pwm_exp *pwm, *temp, *prev_pwm = NULL;
    char fragment[18];

    snprintf(fragment, sizeof(fragment), "bone_pwm_%s", key);
    unload_device_tree(fragment);

    // remove from list
    pwm = exported_pwms;
    while (pwm != NULL)
    {
        if (strcmp(pwm->key, key) == 0)
        {
            //close the fd
            close(pwm->period_fd);
            close(pwm->duty_fd);
            if (prev_pwm == NULL)
                exported_pwms = pwm->next;
            else
                prev_pwm->next = pwm->next;
            temp = pwm;
            pwm = pwm->next;
            free(temp);
        } else {
            prev_pwm = pwm;
            pwm = pwm->next;
        }
    }
    return 0;    
}

void pwm_cleanup(void)
{
    while (exported_pwms != NULL) {
        pwm_disable(exported_pwms->key);
    }
}