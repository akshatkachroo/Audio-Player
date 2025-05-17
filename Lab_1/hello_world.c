#include <stdio.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"

// Variables for background task call count and LED state
volatile alt_u32 task_call_count = 0; // Counter for background task calls
volatile char LED_status = 0;		  // Variable to store LED state

// Background task that toggles an LED and performs some basic processing
int background()
{
	// Set the LED on
	LED_status = LED_status | 0b00000001;
	IOWR(LED_PIO_BASE, 0, LED_status);

	int j;
	int x = 0;
	int grainsize = 4;		 // Number of iterations for the task
	int g_taskProcessed = 0; // Counter for processed tasks
	for (j = 0; j < grainsize; j++)
	{
		g_taskProcessed++;
	}

	// Set the LED off
	LED_status = LED_status ^ 0b00000001;
	IOWR(LED_PIO_BASE, 0, LED_status);
	return x;
}

// Interrupt Service Routine for stimulus input
static void stimulus_in_ISR(void *context, alt_u32 id)
{
	// Set LED on to indicate ISR entry
	LED_status = LED_status | 0b00000100;
	IOWR(LED_PIO_BASE, 0, LED_status);

	// Toggle response output on and then off
	IOWR(RESPONSE_OUT_BASE, 0, 1);
	IOWR(RESPONSE_OUT_BASE, 0, 0);

	// Reset LED state
	LED_status = LED_status ^ 0b00000100;
	IOWR(LED_PIO_BASE, 0, LED_status);

	// Clear ISR flag
	IOWR(STIMULUS_IN_BASE, 3, 0);
}

// Main function to set up and run the test using either polling or interrupt method
int main()
{
	int method = -1;
	int all_button = 0;
	int pb0 = 0;

	while (1)
	{
		method = IORD(SWITCH_PIO_BASE, 0) & 0b1; // Read method from switch input
		pb0 = 0;

		// If polling method is selected
		if (method)
		{
			printf("Polling method selected.\n\nPeriod, Pulse_Width, BG_Tasks Run, Latency, Missed, Multiple.\n\nPlease, press PB0 to continue.\n");

			// Wait for button press to proceed
			while (!pb0)
			{
				all_button = IORD(BUTTON_PIO_BASE, 0);
				pb0 = (~all_button) & 0b1;
			}

			int pulse_width = 0;
			int busy = -1;
			int safe_BG_tasks = 0;

			// Test loop for different period values
			for (int period = 2; period <= 5000; period += 2)
			{
				// Start test run by toggling LED 1
				LED_status = LED_status | 0b00000010;
				IOWR(LED_PIO_BASE, 0, LED_status);
				LED_status = LED_status ^ 0b00000010;
				IOWR(LED_PIO_BASE, 0, LED_status);

				pulse_width = period / 2;

				// Set period and pulse width in EGM
				IOWR(EGM_BASE, 2, period);
				IOWR(EGM_BASE, 3, pulse_width);

				// Enable EGM for this test
				IOWR(EGM_BASE, 0, 1);

				task_call_count = 0;
				safe_BG_tasks = 0;

				// Wait for the first stimulus
				busy = IORD(EGM_BASE, 1);
				while ((!IORD(STIMULUS_IN_BASE, 0)) && busy)
				{
					busy = IORD(EGM_BASE, 1);
				}

				// Toggle response output
				IOWR(RESPONSE_OUT_BASE, 0, 1);
				IOWR(RESPONSE_OUT_BASE, 0, 0);

				// Wait for stimulus input to alternate between 0 and 1 to count # of safe tasks
				while ((IORD(STIMULUS_IN_BASE, 0)) && busy)
				{
					task_call_count++;
					background();
					busy = IORD(EGM_BASE, 1);
				}
				while ((!IORD(STIMULUS_IN_BASE, 0)) && busy)
				{
					task_call_count++;
					background();
					busy = IORD(EGM_BASE, 1);
				}
				safe_BG_tasks = task_call_count - 1;

				// Main polling loop
				while (busy)
				{
					if (IORD(STIMULUS_IN_BASE, 0))
					{
						// If stimulus detected, toggle response
						IOWR(RESPONSE_OUT_BASE, 0, 1);
						IOWR(RESPONSE_OUT_BASE, 0, 0);

						// Run background tasks until period ends
						for (int j = 0; j < safe_BG_tasks; j++)
						{
							if ((IORD(EGM_BASE, 1)))
							{
								task_call_count++;
								background();
							}
						}
					}
					busy = IORD(EGM_BASE, 1);
				}

				// Gather statistics
				alt_u16 avg_latency = IORD(EGM_BASE, 4);
				alt_u16 missed = IORD(EGM_BASE, 5);
				alt_u16 multi = IORD(EGM_BASE, 6);

				// Print results
				printf("%d, %d, %u, %d, %d, %d \n", period, pulse_width, task_call_count, avg_latency, missed, multi);

				// Disable EGM
				IOWR(EGM_BASE, 0, 0);
			}
		}
		else
		{ // If interrupt method is selected
			printf("Interrupt method selected.\n\nPeriod, Pulse_Width, BG_Tasks Run, Latency, Missed, Multiple.\n\nPlease, press PB0 to continue.\n");

			// Wait for button press to proceed
			while (!pb0)
			{
				all_button = IORD(BUTTON_PIO_BASE, 0);
				pb0 = (~all_button) & 0b1;
			}

			// Register ISR for stimulus input
			alt_irq_register(STIMULUS_IN_IRQ, (void *)0, stimulus_in_ISR);

			// Disable EGM
			IOWR(EGM_BASE, 0, 0);

			int busy = -1;
			alt_u16 pulse_width = 0;

			// Test loop for different period values
			for (int period = 2; period <= 5000; period += 2)
			{
				// Start test run by toggling LED
				LED_status = LED_status | 0b00000010;
				IOWR(LED_PIO_BASE, 0, LED_status);
				LED_status = LED_status ^ 0b00000010;
				IOWR(LED_PIO_BASE, 0, LED_status);

				pulse_width = period / 2;

				// Set period and pulse width in EGM
				IOWR(EGM_BASE, 2, period);
				IOWR(EGM_BASE, 3, pulse_width);

				// Enable EGM and stimulus interrupt
				IOWR(EGM_BASE, 0, 1);
				IOWR(STIMULUS_IN_BASE, 2, 1);

				task_call_count = 0;

				busy = IORD(EGM_BASE, 1);

				// Run background tasks until period ends
				while (busy)
				{
					task_call_count++;
					background();
					busy = IORD(EGM_BASE, 1);
				}

				// Gather statistics
				alt_u16 avg_latency = IORD(EGM_BASE, 4);
				alt_u16 missed = IORD(EGM_BASE, 5);
				alt_u16 multi = IORD(EGM_BASE, 6);

				// Print results
				printf("%d, %d, %u, %d, %d, %d \n", period, pulse_width, task_call_count, avg_latency, missed, multi);

				// Disable EGM
				IOWR(EGM_BASE, 0, 0);
			}
		}
	}
}