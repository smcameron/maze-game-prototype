
#define SCREEN_XDIM 132
#define SCREEN_YDIM 132

void FbInit(void);
void plot_point(int x, int y, void *context);
void FbSwapBuffers(void);
void FbLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2);
void FbHorizontalLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2);
void FbVerticalLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2);
void FbClear(void);
void restore_original_input_mode(void);
void set_nonblocking_input_mode(void);
int wait_for_keypress(void);
int get_keypress(void);
void FbMove(unsigned char x, unsigned char y);
void FbWriteLine(char *s);
void itoa(char *string, int value, int base);
int abs(int x);
