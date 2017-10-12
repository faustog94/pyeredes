#include "stdafx.h"
#include "pye.h"
#include "rs232.c"

using namespace std;

int main(int argc, char *argv[]) {

	//Tomar el primer y segundo parametro para determinar la funcion y el puerto
	//Primer parametro (argv[1]): "emisor" o "receptor"
	//Segundo parametro (argv[2]): numero de puerto

	//El indice del puerto empieza en 0
	// por lo tanto argv[2]=0 corresponde al puerto COM1

	if (argc == 3 && !strcmp(argv[1], "emisor")) {
		port = atoi(argv[2]) - 1;
		cout << "Modo: " << argv[1] << endl;
		cout << "Puerto: " << port+1 << endl;
		openPort();
		sender();
	}
	else if (argc == 3 && !strcmp(argv[1], "receptor")) {
		port = atoi(argv[2]) - 1;
		cout << "Modo: " << argv[1] << endl;
		cout << "Puerto: " << port+1 << endl;
		openPort();
		receiver();
	}
	else {
#ifdef _DEBUG
#ifdef _debug_s
		port = 1; //Puerto COM2
		cout << "Modo: " << "debug" << endl;
		cout << "Puerto: " << port+1 << endl;
		openPort();
		sender();
#else
		port = 2; //Puerto COM3
		cout << "Modo: " << "debug" << endl;
		cout << "Puerto: " << port+1 << endl;
		openPort();
		receiver();
#endif
#else
		cout << "El programa requiere dos parametros" << endl;
		cout << "Uso: pye.exe ( emisor | receptor ) <puerto COM>" << endl;
		return 1;
#endif
	}

    return 0;
}

void openPort() {
	//Parametros para el puerto serie
	int bdrate = 9600;
	char mode[] = { '8','N','1',0 };
	
	//Abrir el puerto indicado en la variable 'port'
	RS232_OpenComport(port, bdrate, mode);
}

void sender() {
	bool salir = false;

	while (!salir) {
		//Preparar los buffer
		packet p_buffer = "";
		frame *f_buffer = new frame();
		frame *ack_buffer = new frame();
		tipo_evento event;

		//Capturar entrada de la capa de red (simulada)
		//Si no hay tramas en espera
		if (!frame_offset) fromNetwork(&p_buffer);

		//Salir si se ingresa una cadena vacia
		if (!strcmp(p_buffer, "")) {
			salir = true;
			continue;
		}

		//Copiar el paquete a la carga util de la trama
		//strcpy_s(f_buffer->data, MAX_PACKET_SIZE, p_buffer);
		strncpy(f_buffer->data, p_buffer, MAX_PACKET_SIZE);


		//Indicar e incrementar numero de secuencia
		f_buffer->seq = nseq++;

		//Indicar tipo de trama
		f_buffer->tipo = data_frame;

	transmitir:
		//Enviar trama al medio
		toPhysical(f_buffer);
		
		//Esperar respuesta mientras se cumpla
		// waitEvent != 0 -> la funcion NO retorno con exito
		// event != physical_ready -> no se recibio una trama en la interfaz
		// event != ack_timeout -> no se agoto el tiempo de espera
		wait_ack = true;
		while ((waitEvent(&event) || event != physical_ready) && !(event == ack_timeout));
		//Verificar si se agoto el tiempo de espera, caso afirmativo retransmitir la trama
		if (event == ack_timeout) goto transmitir;
		//Caso contrario recuperar la trama del medio
		wait_ack = false;
		fromPhysical(ack_buffer);
		//Verificar que el tipo y numero de secuencia de la trama sean correctos, caso contrario retransmitir
		if (!(ack_buffer->tipo == ack_frame && ack_buffer->seq == nseq - 1)) {
			delete ack_buffer;
			goto transmitir;
		}
		//Si la trama se envio correctamente, liberar la memoria para la proxima trama
		else {
			delete ack_buffer;
			delete f_buffer;
		}
	}

	return;
}

void receiver() {
	bool salir = false;

	while (!salir) {
		//Preparar los buffer
		packet p_buffer;
		frame *f_buffer = new frame();
		frame *ack_buffer = new frame();
		tipo_evento event;

		//Esperar llegada de un paquete
	esperar:
		while (waitEvent(&event) || event != physical_ready);
		
		//Recuperar paquete del medio
		fromPhysical(f_buffer);

		//Si la trama es erronea, volver a esperar
		
		//Verificar que la trama llego correctamente
		if (f_buffer->tipo == bad_frame) goto esperar;

		//Verificar que la trama sea la esperada
		if (f_buffer->seq != nseq) {
			//Si la trama no es la esperada reenviar el ultimo ACK
			ack_buffer->seq = nseq - 1;
			ack_buffer->tipo = ack_frame;
			toPhysical(ack_buffer);
			goto esperar;
		}

		//Extraer carga util
		//memcpy_s(&p_buffer, MAX_PACKET_SIZE, f_buffer->data, MAX_PACKET_SIZE);
		memcpy(&p_buffer, f_buffer->data, MAX_PACKET_SIZE);


		//Enviar ACK al emisor e incrementar numero de secuencia
		ack_buffer->seq = nseq++;
		ack_buffer->tipo = ack_frame;
		toPhysical(ack_buffer);

		//Verificar si se espera otra trama para completar el paquete
		//Si falta otra trama esperarla antes de enviar el paquete a la capa de red
		if (next_frame) {
			goto esperar;
		}
		//Enviar paquete a la capa de red
		toNetwork(&p_buffer);
	}

	return;
}

int waitEvent(tipo_evento *e) {
	//Monitorear eventos de red
	unsigned int n = 0, i = 0;
	bool timeout = false;
	unsigned char tmp[MEDIA_BUFFER_SIZE] = "";
	unsigned char *buf_ptr = buffer;

	//Leer de la entrada a un buffer temporal cada 100ms
	// o hasta que se agote el temporizador (en caso de esperar un ACK)
	while (!timeout) {
		//n: bytes recibidos
		n = RS232_PollComport(port, tmp, MEDIA_BUFFER_SIZE);
	recibir:
		//Copiar los datos recibidos a un buffer secundario
		//memcpy_s(buf_ptr, MEDIA_BUFFER_SIZE, tmp, MEDIA_BUFFER_SIZE - (buf_ptr - buffer));
		memcpy(buf_ptr, tmp, MEDIA_BUFFER_SIZE);
		buf_ptr += n;
		if (n > 0) {
			//Si se recibió algo dar tiempo a que se llene el buffer del puerto antes de leerlo
			Sleep(100);
			if(n = RS232_PollComport(port, tmp, MEDIA_BUFFER_SIZE))
				goto recibir;
			//Informar evento una vez recibidos todos los datos (n=0, no hay mas datos)
			*e = physical_ready;
			return 0;
		}
		//Si se espera un ACK
		if (wait_ack) {
			//Incrementar el temporizador en 1 cada 100ms (para un total de 500ms antes del timeout)
			if (i >= 5) {
				*e = ack_timeout;
				return 0;
			}
			Sleep(100);
			i++;
		}
	}

	return 1;
}

void fromNetwork(packet *p) {
	//Recuperar paquete de la capa de red (simulado por consola)
	string entrada;
	getline(cin, entrada);
	//strcpy_s(*p, MAX_PACKET_SIZE, entrada.c_str());
	strncpy(*p, entrada.c_str(), MAX_PACKET_SIZE);
}

void toNetwork(packet *p) {
	//Enviar paquete recibido a la capa de red (simulado por consola)
	cout << *p << endl;
}

void fromPhysical(frame *f) {
	//Leer datos del medio, formar la trama y comprobar errores
	//Descartar tramas incorrectas

	//Preparar los buffer
	// codigo de detección de errores
	char error[ERROR_DET_SIZE] = "";
	// trama entrante (con relleno)
	char bits[MAX_FRAME_SIZE] = "";
	
	//Leer del buffer hasta encontrar inicio y fin de trama y guardarla en 'bits'
	unsigned char *f_start = reinterpret_cast<unsigned char*>(memchr(buffer, 0x7e, (MEDIA_BUFFER_SIZE)));
	unsigned char *f_end = reinterpret_cast<unsigned char*>(memchr((f_start + 1), 0x7e, (MEDIA_BUFFER_SIZE - ((f_start + 1) - buffer))));
	
verificar:
	//Verificar que se encontro inicio y fin de trama
	if (!(f_start && f_end)) {
		cout << "No se encontro inicio o fin de trama en el buffer" << endl;
		f->tipo = bad_frame;
		return;
	}

	//Verificar que no se perdio sincronizacion
	if ((f_end - f_start) <= 1) {
		//Hay 2 byte bandera juntos, el fin de trama corresponde a inicio de trama
		f_start = f_end;
		//Es necesario buscar un nuevo fin de trama
		f_end = reinterpret_cast<unsigned char*>(memchr((f_start + 1), 0x7e, (MEDIA_BUFFER_SIZE - ((f_start + 1) - buffer))));
		goto verificar;
	}
	
	unsigned char *s_ptr;
	s_ptr = f_start;

	//Leer tipo, numero de secuencia y numero de proxima trama del buffer
	f->tipo = static_cast<tipo_trama>(*(++s_ptr));
	f->seq = static_cast<char>(*(++s_ptr));
	next_frame = static_cast<char>(*(++s_ptr));

	//Contador para el tamaño de la cola
	int tail_size = 0;

	//Leer la carga util (deshacer relleno de bits) y calcular el codigo de deteccion de error
	for (int i = 0; i < MAX_PACKET_SIZE; i++) {
		//Incrementar el puntero al buffer
		++s_ptr;
		
		//Si es un caracter de escape, saltearlo
		if (*s_ptr == 0xff) {
			++s_ptr;
		}

		//Copiar en la posicion actual de la carga util el valor apuntado del buffer
		f->data[i] = *s_ptr;

		//Calcular el codigo de error para el byte leido
		error[(i % ERROR_DET_SIZE)] ^= *s_ptr;

		//Incrementar el tamaño de la cola hasta llegar al maximo
		if (i < ERROR_DET_SIZE) {
			tail_size++;
		}

		//La carga util esta delimitada por un caracter nulo
		if (*s_ptr == '\0') break;
	}

	if (memcmp(error, ++s_ptr, tail_size)) {
		//Los codigos no coinciden, hay error en la trama
		cout << "Descartando trama erronea..." << endl;
		f->tipo = bad_frame;
	}
}

void toPhysical(frame *f) {
	//Preparar los buffer
	// codigo de detección de errores
	char error[ERROR_DET_SIZE] = "";
	// trama
	char bits[MAX_FRAME_SIZE] = "";
	
	//Tamaño de la cola
	int tail_size = 0;

	//Codificar con byte bandera y relleno de bytes
	unsigned int d_size;
	char *d_ptr;
	d_ptr = bits;

	//Byte bandera de inicio de trama
	*(d_ptr) = 0x7e;

	//Tipo de trama
	*(++d_ptr) = static_cast<char>(f->tipo);

	//Numero de secuencia
	*(++d_ptr) = f->seq;
	
	//Indicador de proxima trama (para paquetes divididos)
	//Se inicializa en cero, si es necesario se cambia despues
	*(++d_ptr) = 0;

	//Tamaño actual de la trama
	d_size = 5; //Contando el byte bandera de fin de trama

	char *s_ptr;
	s_ptr = (f->data);

	//Introducir la carga util en el buffer de envio
	for (int i = frame_offset; i < MAX_PACKET_SIZE; i++) {
		//Si el tamaño de la trama ya es el maximo o 1 menos (se utilizan hasta 2 bytes por iteracion)
		// marcar el desplazamiento actual en el buffer de entrada y finalizar la trama
		if (d_size >= MAX_FRAME_SIZE - ERROR_DET_SIZE - 1) {
			frame_offset = i;
			break;
		}

		//Si se encuentra un byte bandera o de escape, introducir un caracter de escape antes del mismo
		if (s_ptr[i] == 0x7e || s_ptr[i] == 0xff) {
			*(++d_ptr) = 0xffu;
			d_size++;
		}

		//Copiar el byte de entrada al buffer de envio
		*(++d_ptr) = s_ptr[i];
		d_size++;

		//Si se itero por todo el paquete (se completo el bucle), reiniciar el desplazamiento
		if ( (i + 1) == MAX_PACKET_SIZE ) frame_offset = 0;

		//Calcular el codigo de error para el byte actual
		error[(i % ERROR_DET_SIZE)] ^= s_ptr[i];
		
		//Incrementar el tamaño de la cola hasta el maximo
		if (i < ERROR_DET_SIZE) {
			tail_size++;
		}

		//La carga util esta delimitada por un caracter nulo
		//Si se encuentra finalizar, la trama y reiniciar el desplazamiento
		if (s_ptr[i] == '\0') {
			frame_offset = 0;
			break;
		}
	}

	//Si se guardo una posicion de desplazamiento en el buffer de entrada
	// se debe enviar otra trama con la informacion restante del paquete
	if(frame_offset)
		d_ptr[3] = f->seq + 1; //Indicador de proxima trama

	//Agregar cola a la trama
	//strcat_s(bits, MAX_FRAME_SIZE, error); //No funciona, copia en el primer nulo de la trama
	//strcpy_s(++d_ptr, ERROR_DET_SIZE, error); //Tampoco funciona, se detiene en el primer nulo del error
	//memcpy_s(++d_ptr, ERROR_DET_SIZE, error, tail_size);
	memcpy(++d_ptr, error, tail_size);

	//Insertar byte bandera de fin de trama
	*(d_ptr + tail_size) = 0x7e;

	//Enviar trama al medio
	RS232_rawsend(port, bits, d_size+tail_size);

	return;
}
