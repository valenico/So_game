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

#include "client_kit.h"

int udp_socket;
pthread_t runner_thread;

int main(int argc, char **argv) {

	if (argc<2) {
		printf("usage: %s <server_address>\n", argv[1]);
		exit(-1);
	}

	World world;
	Vehicle* vehicle;

	int ret;
	
	// these come from server
	Image* map_elevation;
	Image* map_texture;
	Image* vehicle_texture;
	int my_id = -1;
	
	vehicle_texture = get_vehicle_texture(); 
	char* buf = (char*)malloc(sizeof(char) * BUFLEN);	
	int socket_desc = tcp_client_setup();
	
	
	// REQUEST AND GET AN ID
	clear(buf);
	IdPacket* id_packet = id_packet_init(GetId, my_id);	

	tcp_send(socket_desc , &id_packet->header);	     // Requesting id
	ret = tcp_receive(socket_desc , buf);          // Receiving id
	ERROR_HELPER(ret, "Cannot receive from tcp socket");
	
	Packet_free(&id_packet->header); // free to use the same IdPacket to receive
	
	id_packet = (IdPacket*)Packet_deserialize(buf, ret); // Id received!
	my_id = id_packet->id;
	
	///if(DEBUG) printf("Id received : %d\n", my_id);
	Packet_free(&id_packet->header);
	
	welcome_client(my_id);


    // REQUEST AND GET VEHICLE TEXTURE
	ImagePacket* vehicleTexture_packet;
	if(vehicle_texture) {
		vehicleTexture_packet = image_packet_init(PostTexture, vehicle_texture, my_id);    // client chose to use his own image
		tcp_send(socket_desc , &vehicleTexture_packet->header);
	} else {
		vehicleTexture_packet = image_packet_init(GetTexture, NULL, my_id); // client chose default vehicle image
		
		tcp_send(socket_desc , &vehicleTexture_packet->header);	
		ret = tcp_receive(socket_desc , buf);
		ERROR_HELPER(ret, "Cannot receive from tcp socket");
		
		Packet_free(&vehicleTexture_packet->header); // free to use the same ImagePacket to receive
		vehicleTexture_packet = (ImagePacket*)Packet_deserialize(buf, ret);
		
		if( (vehicleTexture_packet->header).type != PostTexture || vehicleTexture_packet->id <= 0) {
			fprintf(stderr,"Error: Image corrupted");
			exit(EXIT_FAILURE);			
		}
		///if(DEBUG) printf("%s VEHICLE TEXTURE RECEIVED FROM SERVER\n", TCP_SOCKET_NAME);
		
		vehicle_texture = vehicleTexture_packet->image;
    }


    // REQUEST AND GET ELEVATION MAP    
    clear(buf);
    ImagePacket* elevationImage_packet = image_packet_init(GetElevation, NULL, 0);
    tcp_send(socket_desc , &elevationImage_packet->header);
       
    ret = tcp_receive(socket_desc , buf);
    ERROR_HELPER(ret, "Cannot receive from tcp socket");
    
    Packet_free(&elevationImage_packet->header); // free to use the same ImagePacket to receive
    elevationImage_packet = (ImagePacket*)Packet_deserialize(buf, ret);
	
	if( (elevationImage_packet->header).type != PostElevation || elevationImage_packet->id != 0) {
		fprintf(stderr,"Error: Image corrupted");
		exit(EXIT_FAILURE);		
	}
	///if(DEBUG) printf("%s ELEVATION MAP RECEIVED FROM SERVER\n", TCP_SOCKET_NAME);
	
	map_elevation = elevationImage_packet->image;
	
	
	
	// REQUEST AND GET SURFACE TEXTURE
	clear(buf);
	ImagePacket* surfaceTexture_packet = image_packet_init(GetTexture, NULL, 0);
    tcp_send(socket_desc , &surfaceTexture_packet->header);
	
    ret = tcp_receive(socket_desc , buf); 
    ERROR_HELPER(ret, "Cannot receive from tcp socket");
    
    Packet_free(&surfaceTexture_packet->header); // free to use the same ImagePacket to receive
    surfaceTexture_packet = (ImagePacket*)Packet_deserialize(buf, ret);
	
	if( (surfaceTexture_packet->header).type != PostTexture || surfaceTexture_packet->id != 0) {
		fprintf(stderr,"Error: Image corrupted");
		exit(EXIT_FAILURE);		
	}
	///if(DEBUG) printf("%s SURFACE TEXTURE RECEIVED FROM SERVER\n", TCP_SOCKET_NAME);
	
    map_texture = surfaceTexture_packet->image;
	
	// construct the world
	World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
	vehicle=(Vehicle*) malloc(sizeof(Vehicle));
	
	Vehicle_init(vehicle, &world, my_id, vehicle_texture);
	World_addVehicle(&world, vehicle);
	
	free(buf);	
	
	/*** UDP PART NOTIFICATION ***/
	pthread_t connection_checker;
	
	UpdaterArgs runner_args = {
		.run=1,
		.id = my_id,
		.tcp_desc = socket_desc,
		.texture = vehicle_texture,
		.vehicle = vehicle,
		.world = &world
	};
	
	
	ret = pthread_create(&runner_thread, NULL, updater_thread, &runner_args);
	PTHREAD_ERROR_HELPER(ret, "Error: failed pthread_create runner thread");
		
	ret = pthread_create(&connection_checker, NULL, connection_checker_thread, &runner_args);
	PTHREAD_ERROR_HELPER(ret, "Error: failed pthread_create connection_checker thread");
	
	WorldViewer_runGlobal(&world, vehicle, &argc, argv);
	runner_args.run=0;
	
	ret = pthread_join(runner_thread, NULL);
	if(ret!=0 && errno != ESRCH) PTHREAD_ERROR_HELPER(ret, "Error: failed join udp thread"); //if errno = ESRCH thread has already 
	                                                                                         //been ended from connection_checker
	
	ret = pthread_cancel(connection_checker);
	if(ret < 0 && errno != ESRCH) PTHREAD_ERROR_HELPER(ret , "Error: failed cancel connection_checker thread"); //if errno = ESRCH thread has already ended
	

	World_destroy(&world);
	
	ret = close(udp_socket);
	if(ret < 0 && errno != EBADF) ERROR_HELPER(ret , "Error: cannot close udp socket"); // if errno = EBADF socket has already 
	                                                                                    // been closed from connection_checker	
	ret = close(socket_desc);
	if(ret < 0 && errno != EBADF) ERROR_HELPER(ret , "Error: cannot close udp socket"); 
	
	Packet_free(&vehicleTexture_packet->header);
	Packet_free(&elevationImage_packet->header);
	Packet_free(&surfaceTexture_packet->header);	
	
	Vehicle_destroy(vehicle);
	
	return EXIT_SUCCESS;             
}

void *updater_thread(void *args) {
	
	UpdaterArgs* arg = (UpdaterArgs*) args;

	// variables
	int id = arg->id;
	World *world = arg->world;
	Vehicle *vehicle = arg->vehicle;

	// creating socket udp
	struct sockaddr_in si_other;
	udp_socket = udp_client_setup(&si_other);

    int ret;

	char buffer[BUFLEN];
    
	while(arg->run) {

		float r_f_update = vehicle->rotational_force_update;
		float t_f_update = vehicle->translational_force_update;

		// create vehicle_packet
		VehicleUpdatePacket* vehicle_packet = vehicle_update_init(world, id, r_f_update, t_f_update);
		udp_send(udp_socket, &si_other, &vehicle_packet->header);
		
        clear(buffer); 	// we can use the same buffer to receive
		
		ret = udp_receive(udp_socket, &si_other, buffer);
		WorldUpdatePacket* wu_packet = (WorldUpdatePacket*)Packet_deserialize(buffer, ret);		
		client_update(wu_packet, arg->tcp_desc, world);
 		
		usleep(30000);
	}
	return EXIT_SUCCESS;
}

void *connection_checker_thread(void* args){
	UpdaterArgs* arg = (UpdaterArgs*) args;
	int tcp_desc = arg->tcp_desc;
	int ret;
	char c;
	
	while(1){

		ret = recv(tcp_desc , &c , 1 , MSG_PEEK); // this only checks if recv returns 0, without removing data from queue
		if(ret < 0 && errno == EINTR) continue;
		ERROR_HELPER(ret , "Error on receive in connection checker thread"); 
		
		if(ret == 0) break; // server has quit
		usleep(30000);
	}

	arg->run = 0;	
	
	ret = pthread_cancel(runner_thread);                                                             // server has quit, udp thread is no longer necessary
	if(ret < 0 && errno != ESRCH) PTHREAD_ERROR_HELPER(ret , "Error: failed cancel runner_thread "); // if errno = ESRCH thread has already ended
	
	Client_siglePlayerNotification();
	
	ListItem* item = arg->world->vehicles.first;
	
	while(item) {
		Vehicle* v = (Vehicle*)item;
		item = item->next;
		if(v->id != arg->id){
			World_detachVehicle(arg->world , v);
			update_info(arg->world, v->id , 0);
		}			
	}
	
	ret = close(udp_socket);
	if(ret < 0 && errno != EBADF) ERROR_HELPER(ret , "Error: cannot close udp socket"); // if errno = EBADF socket has already been closed 
	
	ret = close(tcp_desc);
	if(ret < 0 && errno != EBADF) ERROR_HELPER(ret , "Error: cannot close tcp socket");
	
	pthread_exit(EXIT_SUCCESS);
}
