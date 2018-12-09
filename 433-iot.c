#include <wiringPi.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

#include "ringbuf.h"
#include "hexdump.h"

#define RING_BUFFER_SIZE 256
#define SYNC_LENGTH 4000
#define SEP_LENGTH 400
#define BIT1_LENGTH 2000
#define BIT0_LENGTH 1000
#define BIT_CORRECTION 200
#define CHANGE_COUNT 76
#define DATA_PIN 2 // wiringPi GPIO 2 (P1.13)

struct ringbuf_t *buffer;

pthread_mutex_t last_sensor_value_mutex;
pthread_mutex_t isr_handler_condition_mutex;
pthread_cond_t isr_handler_condition;

int is_duration_separator(unsigned long duration)
{
	return duration > (SEP_LENGTH - 150) && duration < (SEP_LENGTH + 150);
}

int is_duration_sync(unsigned long duration)
{
	return duration > (SYNC_LENGTH - 1000) && duration < (SYNC_LENGTH + 1000);
}

int is_duration_low_bit(unsigned long duration)
{
	return duration < BIT0_LENGTH + BIT_CORRECTION && duration > BIT0_LENGTH - BIT_CORRECTION;
}

int is_duration_high_bit(unsigned long duration)
{
	return duration < BIT1_LENGTH + BIT_CORRECTION && duration > BIT1_LENGTH - BIT_CORRECTION;
}

char pulse_from_duration(unsigned int duration)
{
	if (is_duration_sync(duration)) {
		return 'S';
	}	
	else if (is_duration_low_bit(duration)) {
		return '0';
	}
	else if (is_duration_high_bit(duration)) {
		return '1';
	}

	return 0;
}

struct sensor_t {
	uint32_t sensor_id;
	uint32_t pad1;
	int32_t temperature;
	int32_t pad2;
	uint16_t crc;
} last_sensor_value;

void isr_handler()
{
	static unsigned long lastTime = 0;
	static unsigned int syncCount = 0;
	static unsigned long t0 = 0;
	static unsigned long t1 = 0;
	static void *sync1 = 0;
	static void *sync2 = 0;

	long time = micros();
	unsigned int duration = time - lastTime;
	lastTime = time;

	char pulse = pulse_from_duration(duration);
	ringbuf_memcpy_into(buffer, &pulse, sizeof(pulse));

	t0 = t1;
	t1 = duration;
    
	if (is_duration_separator(t0) && is_duration_sync(t1) && digitalRead(DATA_PIN) == HIGH)
	{
		syncCount++;
		if (syncCount == 1)
		{
			sync1 = ringbuf_head(buffer);
		}
		else if (syncCount == 2)
		{
			syncCount = 0;	        
			sync2 = ringbuf_head(buffer);

			unsigned int change_count = abs(sync2 - sync1);
			if (change_count != CHANGE_COUNT)
			{
				sync1 = 0;
				sync2 = 0;
			}
			else
			{
				struct sensor_t temperature;
				memset(&temperature, 0, sizeof(struct sensor_t));
				for (unsigned int i = 1, k = 0; i < CHANGE_COUNT - 3; i = i + 2, k++)
				{
					ringbuf_set_tail(buffer, sync1 + i);
					char s = 0;
					ringbuf_memcpy_from(&s, buffer, sizeof(s));
					uint32_t *value = (uint32_t *)&temperature + (k / 8) * 1;
					if (s == '1')
					{
						*value = (*value << 1) + 1;
					}
					else if (s == '0')
					{
						*value = (*value << 1) + 0;
					}
					else
					{
						return;
					}
				}
				
				pthread_mutex_lock(&last_sensor_value_mutex);
				memcpy(&last_sensor_value, &temperature, sizeof(struct sensor_t));
				pthread_mutex_unlock(&last_sensor_value_mutex);
				pthread_cond_signal(&isr_handler_condition);
			}
		}
	}
}

int main(int argc, char *argv[])
{
	int wiring_pi_init = wiringPiSetup();
	if (wiring_pi_init == 0)
	{
		printf("WiringPi initialized.\r\n");
	}
	else
	{
		printf("WiringPi failed to initialize.\r\n");
		return 1;
	}

	pthread_mutex_init(&last_sensor_value_mutex, NULL);

	buffer = ringbuf_new(RING_BUFFER_SIZE * sizeof(uint32_t));
	
	pthread_mutex_init(&isr_handler_condition_mutex, NULL);
	pthread_cond_init(&isr_handler_condition, NULL);
    
	int wiring_pi_isr_init = wiringPiISR(DATA_PIN, INT_EDGE_BOTH, &isr_handler);
	if (wiring_pi_isr_init != 0)
	{
		return 1;
	}

	for (;;)
	{
		pthread_mutex_lock(&isr_handler_condition_mutex);
		pthread_cond_wait(&isr_handler_condition, &isr_handler_condition_mutex);	
		pthread_mutex_unlock(&isr_handler_condition_mutex);
		
		pthread_mutex_lock(&last_sensor_value_mutex);
		printf("sensor_id: %d\n", last_sensor_value.sensor_id);
		printf("temperature: %d\n", last_sensor_value.temperature);
		printf("\n");
		pthread_mutex_unlock(&last_sensor_value_mutex);
	}
	
	return 0;
}
