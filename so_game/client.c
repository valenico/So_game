#include <GL/glut.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>  // htons() and inet_addr()
#include <netinet/in.h> // struct sockaddr_in
#include <sys/socket.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "common.h"
#include "so_game_protocol.h"
#include "common.h"


int window;
WorldViewer viewer;
World world;
Vehicle* vehicle;
int numb_of_vehicles;


typedef struct {
  Image *texture;
  volatile int run;
  int id;
} UpdaterArgs;

int connectToServer(void);
void *updater_thread(void *arg);
void sendToServer(int socket_desc, PacketHeader* header);
int receiveFromServer(int socket_desc, char* msg);
void clear(char* buf);


/**  TO REMEMBER	
 * Alla fine, prima di chiudere il server bisogna rilasciare la memoria
 *   - void Packet_free(PacketHeader* h);
 *   - void Image_free(Image* img);
 *   - free(...) varie
 * **/



int main(int argc, char **argv) {
	if (argc<3) {
	printf("usage: %s <server_address> <player texture>\n", argv[1]);
	exit(-1);
	}

	printf("loading texture image from %s ... \n", argv[2]);
	Image* my_texture = Image_load(argv[2]);
	if (my_texture) {
		printf("Done! \n");
	} else {
		printf("Fail! \n");
	}
	
	
	Image* my_texture_for_server = my_texture;
	
	// todo: connect to the server
	//   -get an id
	//   -send your texture to the server (so that all can see you)
	//   -get an elevation map
	//   -get the texture of the surface
	
	numb_of_vehicles = 0;

	// these come from the server
	int my_id = -1;
	Image* map_elevation;
	Image* map_texture;
	Image* my_texture_from_server; //vehicle texture
	
	char* buf = (char*)malloc(sizeof(char) * BUFLEN);
	clear(buf);
	
	//initiate a connection on the socket
	int socket_desc = connectToServer();	
	
	int ret;
	
	// GET AN ID
	PacketHeader id_header;
	id_header.type = GetId;
	
	IdPacket* id_packet = (IdPacket*)malloc(sizeof(IdPacket));
	id_packet->header = id_header;
	id_packet->id = my_id;
	
	// Send id request
	sendToServer(socket_desc , &id_packet->header);	
	
	//receiving id
	ret = receiveFromServer(socket_desc , buf);
	
	// Id received!
	IdPacket* received_packet = (IdPacket*)Packet_deserialize(buf, ret); 
	my_id = received_packet->id;
	
	if(DEBUG) printf("Id received : %d\n", my_id);
	
	
	// SEND YOUR TEXTURE to the server (so that all can see you)
	// server response should assign the surface texture, the surface elevation and the texture to vehicle
	PacketHeader image_header;
	image_header.type = PostTexture;
	ImagePacket* image_packet = (ImagePacket*)malloc(sizeof(ImagePacket));
	image_packet->header = image_header;
	image_packet->id = my_id;
	image_packet->image = my_texture_for_server;
	
	sendToServer(socket_desc , &image_packet->header);	
    
    
    // GET ELEVATION MAP    
    clear(buf);
    ret = receiveFromServer(socket_desc , buf);
    
    ImagePacket* elevation_packet = (ImagePacket*)Packet_deserialize(buf, ret);
	
	
	if( (elevation_packet->header).type == PostElevation && elevation_packet->id == 0) {
		if(DEBUG) printf("%s ELEVATION MAP RECEIVED FROM SERVER\n", TCP_SOCKET_NAME);
	} else {
		if(DEBUG) printf(" ERROR, elevation map not received!\n");
	}
	map_elevation = elevation_packet->image;
	
	Image_save(map_elevation , "./images_test/elevation_map_client.pgm");
	
	
	// GET SURFACE TEXTURE
	clear(buf);
    ret = receiveFromServer(socket_desc , buf); 

    ImagePacket* texture_packet = (ImagePacket*)Packet_deserialize(buf, ret);
	
	if( (texture_packet->header).type == PostTexture && texture_packet->id == 0) {
		if(DEBUG) printf("%s SURFACE TEXTURE RECEIVED FROM SERVER\n", TCP_SOCKET_NAME);
	} else {
		if(DEBUG) printf(" ERROR, surface texture not received!\n");
	}
    map_texture = texture_packet->image;
    
    Image_save(map_elevation , "./images_test/map_texture_client.ppm");
    
    
    // GET VEHICLE TEXTURE
	clear(buf);
    ret = receiveFromServer(socket_desc , buf);

    ImagePacket* vehicle_packet = (ImagePacket*)Packet_deserialize(buf, ret);
    
	if( (vehicle_packet->header).type == PostTexture && vehicle_packet->id > 0) {
		if(DEBUG) printf("%s VEHICLE TEXTURE RECEIVED FROM SERVER\n", TCP_SOCKET_NAME);
	} else {
		if(DEBUG) printf(" ERROR, vehicle texture not received!\n");
	}
	my_texture_from_server = vehicle_packet->image;
	
	Image_save(map_elevation , "./images_test/vehicle_texture_client.ppm");
	
	
	
	///if(DEBUG) printf(" map_texture: %s\n", map_texture->data);
	///if(DEBUG) printf(" map_elevation: %s\n", map_elevation->data);
	
	if(DEBUG) printf("%s ALL TEXTURES RECEIVED\n", TCP_SOCKET_NAME);
	
	
	// construct the world
	World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
	vehicle=(Vehicle*) malloc(sizeof(Vehicle));
	
	Vehicle_init(vehicle, &world, my_id, my_texture_from_server);
	World_addVehicle(&world, vehicle);
	numb_of_vehicles += 1;
	
	
	// spawn a thread that will listen the update messages from
	// the server, and sends back the controls
	// the update for yourself are written in the desired_*_force
	// fields of the vehicle variable
	// when the server notifies a new player has joined the game
	// request the texture and add the player to the pool
	/*FILLME*/
	
	
	/*** UDP PART NOTIFICATION ***/
	pthread_t runner_thread;
	pthread_attr_t runner_attrs;
	UpdaterArgs runner_args={
		.texture = my_texture_from_server,
		.run=1,
		.id = my_id
	};
	  
	pthread_attr_init(&runner_attrs);
	pthread_create(&runner_thread, &runner_attrs, updater_thread, &runner_args);
	
	WorldViewer_runGlobal(&world, vehicle, &argc, argv);
	runner_args.run=0;
	
	void* retval;
	pthread_join(runner_thread, &retval);
	

	World_destroy(&world);

	//free allocated memory
	Packet_free(&id_packet->header);
	Packet_free(&image_packet->header);
	Packet_free(&texture_packet->header);
	Packet_free(&elevation_packet->header);
	Packet_free(&vehicle_packet->header);
	free(buf);
	
	return 0;             
}




void *updater_thread(void *arg) {
	
	UpdaterArgs* _arg = (UpdaterArgs*) arg;

	// creo socket udp
	struct sockaddr_in si_other;
    int ret, s, slen=sizeof(si_other);
    
    s=socket(AF_INET, SOCK_DGRAM, 0);
    
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(UDP_PORT);
    si_other.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);	
    


	while(_arg->run) {
	
		
		VehicleUpdatePacket* vehicle_packet = (VehicleUpdatePacket*)malloc(sizeof(VehicleUpdatePacket));
		PacketHeader v_head;
		v_head.type = VehicleUpdate;

		vehicle_packet->header = v_head;
		vehicle_packet->id = _arg->id;
		vehicle_packet->rotational_force = vehicle->rotational_force;
		vehicle_packet->translational_force = vehicle->translational_force;
		
		char vehicle_buffer[BUFLEN];
		int vehicle_buffer_size = Packet_serialize(vehicle_buffer, &vehicle_packet->header);
		
		ret = sendto(s, vehicle_buffer, vehicle_buffer_size , 0 , (struct sockaddr *) &si_other, slen);
		ERROR_HELPER(ret, "Cannot send to server");

		
        char world_buffer[BUFLEN];
		ret = recvfrom(s, world_buffer, sizeof(world_buffer), 0, (struct sockaddr *) &si_other, (socklen_t*) &slen);
		ERROR_HELPER(ret, "Cannot recv from server");
		
		WorldUpdatePacket* deserialized_wu_packet = (WorldUpdatePacket*)Packet_deserialize(world_buffer, ret);		
		int i;
		printf("Number of vehicles: %d\n", deserialized_wu_packet->num_vehicles);
		for(i=0; i<deserialized_wu_packet->num_vehicles; i++)
			printf("v->id = %d\n",deserialized_wu_packet->updates->id);
	
		Vehicle *v = World_getVehicle(&world, deserialized_wu_packet->updates->id);
		
		/**
		if(v == NULL) {
			v = (Vehicle*) malloc(sizeof(Vehicle));
			Vehicle_init(v, &world, deserialized_wu_packet->updates->id, _arg->texture);
			World_addVehicle(&world, v);
		}
		//Mi garantisco che non è il mio, quindi aggiorno il veicolo corrispondente
		if (v->id != vehicle->id) {
			
			printf("v-> x = %f\n", deserialized_wu_packet->updates->x);
			printf("v->y = %f\n", deserialized_wu_packet->updates->y);
			printf("v->theta = %f\n", deserialized_wu_packet->updates->theta);
		}
		**/
		sleep(2);
		//usleep(30000);
	}
	return 0;
}




int connectToServer(void){
	int ret;

	// variables for handling a socket
	int socket_desc;
	struct sockaddr_in server_addr = {0}; // some fields are required to be filled with 0

	// create a socket
	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	ERROR_HELPER(socket_desc, "Could not create socket");

	// set up parameters for the connection
	server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
	server_addr.sin_family      = AF_INET;
	server_addr.sin_port        = htons(TCP_PORT); // network byte order!

	// initiate a connection on the socket
	ret = connect(socket_desc, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in));
	ERROR_HELPER(ret, "Could not create connection");

	if (DEBUG) fprintf(stderr, "Connection established!\n");  
	
	return socket_desc;
}



void sendToServer(int socket_desc, PacketHeader* packet){
	int ret;	
	char to_send[BUFLEN];
	char len_to_send[BUFLEN];
	
	int len =  Packet_serialize(to_send, packet);
	snprintf(len_to_send, BUFLEN , "%d", len);
	
	//if(DEBUG) printf("*** Bytes to send : %s ***\n" , len_to_send);
	
	ret = send(socket_desc, len_to_send, sizeof(long int) , 0);
	ERROR_HELPER(ret, "Cannot send msg to the server");  
	
	ret = send(socket_desc, to_send, len , 0);
	ERROR_HELPER(ret, "Cannot send msg to the server");  

}

int receiveFromServer(int socket_desc, char* msg){
	int ret;
	char len_to_receive[BUFLEN];
	
	ret = recv(socket_desc , len_to_receive , sizeof(long int) , 0);
	ERROR_HELPER(ret, "Cannot receive from server");
	
	int received_bytes = 0;
	int to_receive = atoi(len_to_receive);
	
	while(received_bytes < to_receive){
		ret = recv(socket_desc , msg + received_bytes , to_receive - received_bytes , 0);
	    ERROR_HELPER(ret, "Cannot receive from server");
	    received_bytes += ret;
	    //if(DEBUG) printf("*** Bytes received : %d ***\n" , ret);
	    if(ret==0) break;
	}

	return received_bytes;
}



void clear(char* buf){
	memset(buf , 0 , BUFLEN * sizeof(char));
}


