void clear_screen(void) {
    char *video_memory = (char *)0xB8000;

for (int i = 0; i < 4000; i += 2) {
    video_memory[i] = ' ';
    video_memory[i+1] = 0x07;
  }

}

void kernel_main(void) {
    clear_screen();

    char *video_memory = (char *)0xB8000;

    video_memory[0] = 'M';
    video_memory[1] = 0x0F;
 
    video_memory[2] = 'I';
    video_memory[3] = 0x0F;

    video_memory[4] = 'N';
    video_memory[5] = 0x0F;

    video_memory[6] = 'U';
    video_memory[7] = 0x0F;

    video_memory[8] = 'X';
    video_memory[9] = 0x0F;

    while(1) {

    }
}
