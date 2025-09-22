#include "keyboard_map.h"

#define MAX_TENTATIVAS 6
#define PALAVRA_TAM 5
#define NUM_PALAVRAS 10

const char *lista_palavras[NUM_PALAVRAS] = {
    "TERMO",
    "CASAS",
    "LIVRO",
    "FELIZ",
    "PLANO",
    "GRITO",
    "FORTE",
    "NORTE",
    "LINHA"
};

char palavra_correta[PALAVRA_TAM+1];
char tentativa[PALAVRA_TAM+1];
int tentativas = 0;
int letra_atual = 0;
int ganhou = 0;
int perdeu = 0;

#define LINES 25
#define COLUMNS_IN_LINE 80
#define BYTES_FOR_EACH_ELEMENT 2
#define SCREENSIZE BYTES_FOR_EACH_ELEMENT * COLUMNS_IN_LINE * LINES

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define IDT_SIZE 256
#define INTERRUPT_GATE 0x8e
#define KERNEL_CODE_SEGMENT_OFFSET 0x08

#define ENTER_KEY_CODE 0x1C

extern unsigned char keyboard_map[128];
extern void keyboard_handler(void);
extern char read_port(unsigned short port);
extern void write_port(unsigned short port, unsigned char data);
extern void load_idt(unsigned long *idt_ptr);


unsigned int current_loc = 0;

char *vidptr = (char*)0xb8000;

struct IDT_entry {
	unsigned short int offset_lowerbits;
	unsigned short int selector;
	unsigned char zero;
	unsigned char type_attr;
	unsigned short int offset_higherbits;
};

struct IDT_entry IDT[IDT_SIZE];


void idt_init(void)
{
	unsigned long keyboard_address;
	unsigned long idt_address;
	unsigned long idt_ptr[2];

	keyboard_address = (unsigned long)keyboard_handler;
	IDT[0x21].offset_lowerbits = keyboard_address & 0xffff;
	IDT[0x21].selector = KERNEL_CODE_SEGMENT_OFFSET;
	IDT[0x21].zero = 0;
	IDT[0x21].type_attr = INTERRUPT_GATE;
	IDT[0x21].offset_higherbits = (keyboard_address & 0xffff0000) >> 16;

	/*     Ports
	*	 PIC1	PIC2
	*Command 0x20	0xA0
	*Data	 0x21	0xA1
	*/

	/* ICW1 - begin initialization */
	write_port(0x20 , 0x11);
	write_port(0xA0 , 0x11);

	/* ICW2 - remap offset address of IDT */
	/*
	* In x86 protected mode, we have to remap the PICs beyond 0x20 because
	* Intel have designated the first 32 interrupts as "reserved" for cpu exceptions
	*/
	write_port(0x21 , 0x20);
	write_port(0xA1 , 0x28);

	/* ICW3 - setup cascading */
	write_port(0x21 , 0x00);
	write_port(0xA1 , 0x00);

	/* ICW4 - environment info */
	write_port(0x21 , 0x01);
	write_port(0xA1 , 0x01);
	/* Initialization finished */

	/* mask interrupts */
	write_port(0x21 , 0xff);
	write_port(0xA1 , 0xff);

	/* fill the IDT descriptor */
	idt_address = (unsigned long)IDT ;
	idt_ptr[0] = (sizeof (struct IDT_entry) * IDT_SIZE) + ((idt_address & 0xffff) << 16);
	idt_ptr[1] = idt_address >> 16 ;

	load_idt(idt_ptr);
}

void kb_init(void)
{
	/* 0xFD is 11111101 - enables only IRQ1 (keyboard)*/
	write_port(0x21 , 0xFD);
}

void kprint(const char *str)
{
	unsigned int i = 0;
	while (str[i] != '\0') {
		vidptr[current_loc++] = str[i++];
		vidptr[current_loc++] = 0x07;
	}
}


void kprint_char_color(char c, unsigned char color) {
    vidptr[current_loc++] = c;
    vidptr[current_loc++] = color;
}

void apagarLetra() {
    if (letra_atual > 0) {
        letra_atual--;              // volta uma posição no buffer da tentativa
        tentativa[letra_atual] = '\0'; // limpa a posição

        current_loc -= 2;           // volta cursor (caractere + atributo)
        vidptr[current_loc] = ' ';  // escreve espaço
        vidptr[current_loc + 1] = 0x07; // mantém cor padrão (cinza claro)
    }
}


void kprint_newline(void)
{
	unsigned int line_size = BYTES_FOR_EACH_ELEMENT * COLUMNS_IN_LINE;
	current_loc = current_loc + (line_size - current_loc % (line_size));
}

void clear_screen(void)
{
	unsigned int i = 0;
	while (i < SCREENSIZE) {
		vidptr[i++] = ' ';
		vidptr[i++] = 0x07;
	}
}

unsigned char read_rtc(unsigned char reg) {
    write_port(0x70, reg);           // Select RTC register
    return read_port(0x71);          // Read data from RTC
}

void escolher_palavra() {
    unsigned char semente = read_rtc(0x00); // 0x00: RTC seconds register
    int indice = semente % NUM_PALAVRAS;
    for(int i = 0; i < PALAVRA_TAM; i++)
        palavra_correta[i] = lista_palavras[indice][i];
    palavra_correta[PALAVRA_TAM] = '\0';
}


void inicializar(){
    escolher_palavra();
    clear_screen();
    kprint("Bem-vindo ao Termel: seu Termo no Kernel!");
    kprint_newline();
    kprint("Digite uma palavra de 5 letras e pressione ENTER");
    kprint_newline();
}

void mensagem_ganhou(){
	const char *str_ganhou = "VOCE VENCEU!!";
	kprint_newline();
	kprint(str_ganhou);
}

void mensagem_perdeu(){
	const char *str_perdeu = "GAME OVER! Palavra certa:";
	kprint_newline();
	kprint(str_perdeu);
    kprint_newline();
    kprint(palavra_correta);
}

void kmain(void) {
    clear_screen();
    idt_init();
    kb_init();

    inicializar();

    while(!ganhou && !perdeu);

	if(ganhou)
        mensagem_ganhou();
    
    if(perdeu)
        mensagem_perdeu();
}

// Recebe uma letra do teclado e adiciona à tentativa
void inserirLetra(char c) {
    if(letra_atual < PALAVRA_TAM) {
        tentativa[letra_atual++] = c;
        char str[2] = {c, '\0'};
        kprint(str);
    }
}

void verificarPalavra() {
    if(letra_atual != PALAVRA_TAM) return;

    int acertos = 0;
    unsigned int line_size = BYTES_FOR_EACH_ELEMENT * COLUMNS_IN_LINE;
    current_loc = (current_loc / line_size) * line_size;

    // Arrays auxiliares para controle de letras já usadas
    int usadas_corretas[PALAVRA_TAM] = {0}; // Marca letras já verdes
    int usadas_tentativa[PALAVRA_TAM] = {0}; // Marca letras já amarelas

    // Primeiro passo: marca verdes
    for(int i = 0; i < PALAVRA_TAM; i++) {
        if(tentativa[i] == palavra_correta[i]) {
            kprint_char_color(tentativa[i], 0x0A); // verde
            usadas_corretas[i] = 1;
            usadas_tentativa[i] = 1;
            acertos++;
        } else {
            // Preenche com espaço, será colorido depois
            kprint_char_color(tentativa[i], 0x07);
        }
    }

    // Segundo passo: marca amarelas
    for(int i = 0; i < PALAVRA_TAM; i++) {
        if(usadas_tentativa[i]) continue; // já foi verde

        for(int j = 0; j < PALAVRA_TAM; j++) {
            if(!usadas_corretas[j] && tentativa[i] == palavra_correta[j]) {
                // Reposiciona cursor para colorir letra
                unsigned int pos = current_loc - (PALAVRA_TAM - i) * BYTES_FOR_EACH_ELEMENT;
                vidptr[pos] = tentativa[i];
                vidptr[pos + 1] = 0x0E; // amarelo
                usadas_corretas[j] = 1;
                usadas_tentativa[i] = 1;
                break;
            }
        }
    }

    if (acertos == PALAVRA_TAM){
        ganhou = 1;
    }

    kprint_newline();
    tentativas++;
    letra_atual = 0;

    if (tentativas >= 5 && !ganhou){
        perdeu = 1;
    }
}

void keyboard_handler_main(void) {
    unsigned char status;
    char keycode;

    write_port(0x20, 0x20);

    status = read_port(0x64);
    if(status & 0x01) {
        keycode = read_port(0x60);
        if(keycode < 0) return;

        if(keycode == 0x1C) {
            verificarPalavra();
            return;
        }

		if (keycode == 0x0E) { // Backspace
			apagarLetra();
			return;
		}

        char letra = keyboard_map[(unsigned char)keycode];
        if(letra >= 'a' && letra <= 'z') {
            letra = letra - 'a' + 'A';
            inserirLetra(letra);
        }
    }
}


