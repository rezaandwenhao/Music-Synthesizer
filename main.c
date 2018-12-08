#include "./drivers/inc/vga.h"
#include "./drivers/inc/ISRs.h"
#include "./drivers/inc/LEDs.h"
#include "./drivers/inc/audio.h"
#include "./drivers/inc/HPS_TIM.h"
#include "./drivers/inc/int_setup.h"
#include "./drivers/inc/wavetable.h"
#include "./drivers/inc/pushbuttons.h"
#include "./drivers/inc/ps2_keyboard.h"
#include "./drivers/inc/HEX_displays.h"
#include "./drivers/inc/slider_switches.h"

float make_wave(float f, int t) { 
	int freq, ind, sig;

	freq = (int) f;
	ind = (freq * t) % 48000; // calculate the relative index in the wavetable
	sig = (sine[ind]); // obtain value at certain index of the table and store inside the signal
	return sig;
}

int control_display_wave() { // this is a function that runs a keyboard synthesizer
	short color1 = 333;
	short color2 = 84000;
	short color3 = 20000;
	
	int audio_flag = 0; // flag that keep track of whether there is a valid write to Leftdata and Rightdata registers
	int breakCode_flag = 0; // flag to keep track of whether a break code is sent
	double amp = 1; // volume control variable that increments and decrements the amplitude
	
	char c; // character for keyboard info
	char* mem = &c; // pointer for keyboard info

	int drawCtr = 0;
	int bufferCtr = 0;

	int buffer[320];  // buffer contains the 320 amplitude information of the audio played with respect to time 
	int height[320]; 

	unsigned char makecode[8] = {0x1C,0x1B,0x23,0x2B,0x3B,0x42,0x4B,0x4C};
	int notes[8] = {0,0,0,0,0,0,0,0};
	
	int sounds[8][48000]; // initialize the sound array
	int time = 0;
	for(time=0;time<48000;time++) {
		sounds[0][time] = make_wave(130.813, time); // low C
		sounds[1][time] = make_wave(146.832, time); // note D
		sounds[2][time] = make_wave(164.814, time); // note E
		sounds[3][time] = make_wave(174.614, time); // note F
		sounds[4][time] = make_wave(195.998, time); // note G
		sounds[5][time] = make_wave(220.000, time); // note A
		sounds[6][time] = make_wave(246.942, time); // note B
		sounds[7][time] = make_wave(261.626, time); // high C
	}
	time = 0;



	int_setup(1, (int []){199}); // setting up interrupts for tim0
	int_setup(1, (int[]){200});	// setting up interrupts for tim1
		
	HPS_TIM_config_t tim0; // configuring tim0 and defining its parameters
	tim0.tim = TIM0;
	tim0.timeout = 10;
	tim0.LD_en = 1;
	tim0.INT_en = 1;
	tim0.enable = 1;
	HPS_TIM_config_ASM(&tim0);
	
	HPS_TIM_config_t tim1; // configuring tim1 and defining its parameters
	tim1.tim = TIM1;
	tim1.timeout = 150000;
	tim1.LD_en = 1;
	tim1.INT_en = 1;
	tim1.enable = 1;
	HPS_TIM_config_ASM(&tim1);


	while(1){
		int total_signal = 0; // total signal is initialized to 0 for each iteration
		if(hps_tim0_int_flag) { // once tim0 runs out, it interrupts the processor to write more data to the left-channel and right-channel write FIFOs
			int key = read_ps2_data_ASM(mem); //checks to see if a key has been pressed

			if(key && *mem == 0x55) { // '+' key to increase the volume
				if(!breakCode_flag) { 
					amp = 1.5 * amp; // multiply the volume by 1.5
					if(amp > 100) {
						amp = 100;
					}
				} else {
					breakCode_flag = 0; // set break code flag to 0, which allows note to be played when a key is pressed
				}
			}
			else if(key && *mem == 0x4E) { // '-' key to decrease the volume
				if(!breakCode_flag) { 
					amp = 0.5 * amp; //decrease the volume by half
					if(amp < 0.01) {
						amp = 0.01;
					}
				} else {
					breakCode_flag = 0; // set break code flag to 0, which allows note to be played when a key is pressed
				}
			}

			int i;
			for (i = 0; i < 8; i++) {
				if (key && *mem == makecode[i]) {
					if(!breakCode_flag){ // key is pressed
						notes[i]=1;
					} else { // the program will reach here when the second byte of break code is found
						notes[i]=0;
						breakCode_flag = 0; // set break code flag to 0, which allows note to be played when a key is pressed	
					}	
					
					break;
				}
			}
			
			if(key && *mem == 0xF0) { // the first byte of break code found, which means a key has been released 
					breakCode_flag = 1; // set break code flag to 1
			}
		}

		int j;
		for (j = 0; j < 8; j++ ) {
			total_signal += notes[j]*sounds[j][time]; // loop to sum the signals for all the notes pressed
		}
		total_signal = amp * total_signal; // volume multiplier	
		
		audio_flag = audio_write_data_ASM(total_signal, total_signal); // write total signal to the Leftdata and Rightdata registers

		//if signal data written to the fifos
		if(audio_flag){ 
			if(hps_tim0_int_flag){ 
				hps_tim0_int_flag = 0; //Reset to enable next tim0 interruption
				buffer[bufferCtr] = total_signal;  
				bufferCtr++;
				time++; // both the time and buffer counter are increased
				if(bufferCtr >= 320) 
					bufferCtr = 0;
			}
		}
		if(hps_tim1_int_flag){ // if hps_tim1_int_flag is asserted, the processor is interrupted to draw to the screen
			hps_tim1_int_flag = 0; // the tim1 interrupt flag is reset to 0

			// "clears" the previous waves or horizontal lines
			for(drawCtr = 0; drawCtr < 320 ; drawCtr++){
				VGA_draw_point_ASM(drawCtr, height[(drawCtr) % 320]+60, 0);  
				VGA_draw_point_ASM(drawCtr, height[(drawCtr) % 320]+120, 0);
				VGA_draw_point_ASM(drawCtr, height[(drawCtr) % 320]+180, 0);	
				VGA_draw_point_ASM(drawCtr, 60, 0);	
				VGA_draw_point_ASM(drawCtr, 120, 0);	
				VGA_draw_point_ASM(drawCtr, 180, 0);	
			}

			if(total_signal == 0){
				// no signal, draw three straight lines
				for(drawCtr = 0; drawCtr < 320; drawCtr++){
					VGA_draw_point_ASM(drawCtr, 60, color1); 
					VGA_draw_point_ASM(drawCtr, 120, color2);
					VGA_draw_point_ASM(drawCtr, 180, color3);
					height[drawCtr] = 120; 
				}
			} else {
				// when signal greater than 0, display three waves of different colors on the screen 
				for(drawCtr = 0; drawCtr < 320; drawCtr++){
					height[drawCtr] = buffer[(drawCtr + bufferCtr) % 320] / 300000;
					VGA_draw_point_ASM(drawCtr, height[drawCtr]+60 , color1);  //draw point at p with scaled height
					VGA_draw_point_ASM(drawCtr, height[drawCtr]+120 , color2);
					VGA_draw_point_ASM(drawCtr, height[drawCtr]+180 , color3);
				}
			}
		}
		if(time == 48000) // if time reaches 48000 (the sampling frenquency), reset time to 0
			time = 0; 
	}
	return 0;
}

int main() {
	VGA_clear_pixelbuff_ASM();
	control_display_wave();	
 	return 0;
} 
