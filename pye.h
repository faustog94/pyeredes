#pragma once

#define MEDIA_BUFFER_SIZE 4096
#define MAX_PACKET_SIZE 255
#define MAX_FRAME_SIZE 1400
#define ERROR_DET_SIZE 140

enum tipo_evento {network_ready, physical_ready, ack_timeout};
enum tipo_trama {ack_frame, data_frame, bad_frame};

typedef char packet[MAX_PACKET_SIZE];

typedef struct {
	tipo_trama tipo;
	unsigned char seq;
	packet data;
} frame;

void openPort();

void sender();
void receiver();

int waitEvent(tipo_evento *e);

void fromNetwork(packet *p);
void toNetwork(packet *p);

void fromPhysical(frame *f);
void toPhysical(frame *f);

unsigned char nseq;

unsigned int frame_offset = 0;
unsigned char next_frame = 0;

unsigned int port = 0;

unsigned char buffer[MEDIA_BUFFER_SIZE] = "";

bool wait_ack = false;
