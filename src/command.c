/* mpc
 * (c) 2003-2004 by normalperson and Warren Dukes (shank@mercury.chem.pitt.edu)
 * This project's homepage is: http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "libmpdclient.h"
#include "list.h"
#include "charConv.h"
#include "password.h"
#include "util.h"
#include "status.h"
#include "command.h"
#include "mpc.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/param.h>

#define DIE(args...) do { fprintf(stderr, ##args); return -1; } while(0)

#define SIMPLE_CMD(funcname, libmpdclient_funcname) \
int funcname ( int argc, char ** argv, mpd_Connection * conn ) { \
	libmpdclient_funcname(conn); \
		my_finishCommand(conn); \
		return 1; \
}

#define SIMPLE_ONEARG_CMD(funcname, libmpdclient_funcname) \
int funcname ( int argc, char ** argv, mpd_Connection * conn ) { \
	libmpdclient_funcname(conn, toUtf8(argv[0])); \
		my_finishCommand(conn); \
		return 1; \
}

static void my_finishCommand(mpd_Connection * conn) {
	printErrorAndExit(conn);
	mpd_finishCommand(conn);
	printErrorAndExit(conn);
}

	SIMPLE_CMD(cmd_next, mpd_sendNextCommand)
	SIMPLE_CMD(cmd_pause, mpd_sendPauseCommand)
	SIMPLE_CMD(cmd_prev, mpd_sendPrevCommand)
	SIMPLE_CMD(cmd_stop, mpd_sendStopCommand)
	SIMPLE_CMD(cmd_clear, mpd_sendClearCommand)
	SIMPLE_CMD(cmd_shuffle, mpd_sendShuffleCommand)
SIMPLE_CMD(cmd_update, mpd_sendUpdateCommand)

	SIMPLE_ONEARG_CMD(cmd_save, mpd_sendSaveCommand)
SIMPLE_ONEARG_CMD(cmd_rm, mpd_sendRmCommand)

int cmd_add (int argc, char ** argv, mpd_Connection * conn ) 
{
	List ** lists;
	mpd_InfoEntity * entity;
	ListNode * node;
	mpd_Song * song;
	char * sp;
	char * dup;
	int i;
	int * arglens;
	int len;
	int ret;

	lists = malloc(sizeof(List *)*(argc));
	arglens = malloc(sizeof(int)*(argc));

	/* convert ' ' to '_' */
	for(i=0;i<argc;i++) {
		lists[i] = makeList(free);
		sp = argv[i];
		while((sp = strchr(sp,' '))) *sp = '_';
		arglens[i] = strlen(argv[i]);
	}

	/* get list of songs to add */
	mpd_sendListallCommand(conn,"");
	printErrorAndExit(conn);

	while((entity = mpd_getNextInfoEntity(conn))) {
		if(entity->type==MPD_INFO_ENTITY_TYPE_SONG) {
			song = entity->info.song;
			sp = dup = strdup(fromUtf8(song->file));
			while((sp = strchr(sp,' '))) *sp = '_';
			len = strlen(dup);
			for(i=0;i<argc;i++) {
				if(len<arglens[i]) continue;
				ret = strncmp(argv[i],dup,
						arglens[i]);
				if(ret==0) {
					insertInListWithoutKey(
							lists[i],
							strdup(fromUtf8(
									song->file)));
				}
			}
			free(dup);
		}
		mpd_freeInfoEntity(entity);
	}
	my_finishCommand(conn);

	/* send list of songs */
	mpd_sendCommandListBegin(conn);
	printErrorAndExit(conn);
	for(i=0;i<argc;i++) {
		node = lists[i]->firstNode;
		while(node) {
			printf("adding: %s\n",(char *)node->data);
			mpd_sendAddCommand(conn,toUtf8(node->data));
			printErrorAndExit(conn);
			node = node->nextNode;
		}
		freeList(lists[i]);
	}
	mpd_sendCommandListEnd(conn);
	my_finishCommand(conn);

	/* clean up */
	free(lists);
	return 0;
}

int cmd_del ( int argc, char ** argv, mpd_Connection * conn )
{
	int i,j;
	char * s;
	char * t;
	char * t2;
	int range[2];
	int songsDeleted = 0;
	int plLength = 0;
	char * songsToDel;
	mpd_Status * status;

	status = mpd_getStatus(conn);
	my_finishCommand(conn);
	plLength = status->playlistLength;

	songsToDel = malloc(plLength);
	memset(songsToDel,0,plLength);

	for(i=0;i<argc;i++) {
		if(argv[i][0]=='#') s = &(argv[i][1]);
		else s = argv[i];

		range[0] = strtol(s,&t,10);
		if(s==t)
			DIE("error parsing song numbers from" ": %s\n",argv[i]);
		else if(*t=='-') {
			range[1] = strtol(t+1,&t2,10);
			if(t+1==t2 || *t2!='\0')
				DIE("error parsing range " "from: %s\n",argv[i]);
		}
		else if(*t==')' || *t=='\0') range[1] = range[0];
		else
			DIE("error parsing song numbers from" ": %s\n",argv[i]);

		if(range[0]<=0 || range[1]<=0 || range[1]<range[0])
			DIE("song numbers must be positive: %i-%i\n",range[0],range[1]);

		if(range[1]>plLength)
			DIE("song number does not exist: %i\n",range[1]);

		for(j=range[0];j<=range[1];j++) songsToDel[j-1] = 1;
	}

	mpd_sendCommandListBegin(conn);
	printErrorAndExit(conn);
	for(i=0;i<plLength;i++) {
		if(songsToDel[i]) {
			mpd_sendDeleteCommand(conn,i-songsDeleted);
			printErrorAndExit(conn);
			songsDeleted++;
		}
	}
	mpd_sendCommandListEnd(conn);
	my_finishCommand(conn);

	free(songsToDel);
	return 0;
}

int cmd_play ( int argc, char ** argv, mpd_Connection * conn )
{
	int song;
	int i;

	if(0==argc) song = MPD_PLAY_AT_BEGINNING;
	else {
		char * s;
		char * t;

		for(i=0;i<argc-1;i++) {
			printf("skipping: %s\n",argv[i]);
		}

		if(argv[i][0]=='#') s = &(argv[i][1]);
		else s = argv[i];

		song = strtol(s,&t,10);
		if(s==t || (*t!=')' && *t!='\0'))
			DIE("error parsing song numbers from: %s\n",argv[i]);

		if(song<1)
			DIE("\"%s\" is not a positive integer\n",argv[i]);
		song--;
	}

	mpd_sendPlayCommand(conn,song);
	my_finishCommand(conn);

	return 1;
}

enum SeekMode { RelForward, RelBackward, Absolute };

	static int calculate_seek(int current_time, int change, int mode) {
		if(mode == Absolute)
			return change;
		else if(mode == RelForward)
			return current_time + change;
		else /* RelBackward */
			return current_time - change;
	}

/* TODO: absolute seek times (normalperson) */
int cmd_seek ( int argc, char ** argv, mpd_Connection * conn )
{
	mpd_Status * status = mpd_getStatus(conn);
	my_finishCommand(conn);
	if(status->state==MPD_STATUS_STATE_STOP)
		DIE("not currently playing\n");

	char * arg = argv[0];

	if(!strlen(arg))
		DIE("\"%s\" is not a positive number\n", arg);

	int seekmode = Absolute;

	if(arg[0] == '+') {
		seekmode = RelForward;
		arg++;
	} else if(arg[0] == '-') {
		seekmode = RelBackward;
		arg++;
	}

	char * test;
	int seekchange;

	char * last_char = &arg[strlen(arg)-1];
	if(*last_char == 's') {
		/* absolute seek (in seconds) */

		*last_char = '\0'; /* chop off the s */
		int sec = strtol(arg,&test,10); /* get the # of seconds */

		if(*test!='\0' || sec<0)
			DIE("\"%s\" is not a positive number\n", arg);

		seekchange = sec;

	} else {
		/* percent seek */

		float perc = strtod(arg,&test);
		if(*test!='\0' || perc<0 || perc>100)
			DIE("\"%s\" is not an number between 0 and 100\n",arg);

		seekchange = perc*status->totalTime/100+0.5;
	}

	int seekto = calculate_seek(status->elapsedTime, seekchange, seekmode);

	if(seekto > status->totalTime)
		DIE("seek amount would seek past the end of the song\n");

	mpd_sendSeekCommand(conn,status->song,seekto);
	my_finishCommand(conn);
	mpd_freeStatus(status);
	return 1;
}

int cmd_move ( int argc, char ** argv, mpd_Connection * conn )
{
	int from;
	int to;
	char * test;

	from = strtol(argv[0],&test,10);
	if(*test!='\0' || from<=0)
		DIE("\"%s\" is not a positive " "integer\n",argv[0]);

	to = strtol(argv[1],&test,10);
	if(*test!='\0' || to<=0)
		DIE("\"%s\" is not a positive " "integer\n",argv[1]);

	/* users type in 1-based numbers, mpd uses 0-based */
	from--;
	to--;

	mpd_sendMoveCommand(conn,from,to);
	my_finishCommand(conn);

	return 0;
}

int cmd_playlist ( int argc, char ** argv, mpd_Connection * conn )
{
	mpd_InfoEntity * entity;
	int count = 0;

	mpd_sendPlaylistInfoCommand(conn,-1);
	printErrorAndExit(conn);

	while((entity = mpd_getNextInfoEntity(conn))) {
		if(entity->type==MPD_INFO_ENTITY_TYPE_SONG) {
			mpd_Song * song = entity->info.song;
			if(song->artist && song->title) {
				printf("#%i) %s - ",1+count, fromUtf8(song->artist));
				printf("%s\n",fromUtf8(song->title));
			}
			else printf("#%i) %s\n",1+count, fromUtf8(song->file));
			count++;
		}
		mpd_freeInfoEntity(entity);
	}

	my_finishCommand(conn);

	return 0;
}

int cmd_listall ( int argc, char ** argv, mpd_Connection * conn )
{
	mpd_InfoEntity * entity;
	char * listall = "";
	int i=0;

	if(argc>0) listall = toUtf8(argv[i]);

	do {
		mpd_sendListallCommand(conn,listall);
		printErrorAndExit(conn);

		while((entity = mpd_getNextInfoEntity(conn))) {
			if(entity->type==MPD_INFO_ENTITY_TYPE_SONG) {
				mpd_Song * song = entity->info.song;
				printf("%s\n",fromUtf8(song->file));
			}
			mpd_freeInfoEntity(entity);
		}

		my_finishCommand(conn);

	} while(++i<argc&& (listall = toUtf8(argv[i])));

	return 0;
}

int cmd_ls ( int argc, char ** argv, mpd_Connection * conn )
{
	mpd_InfoEntity * entity;
	char * ls = "";
	int i = 0;
	char * sp;
	char * dp;

	if(argc>0) ls = toUtf8(argv[i]);

	do {
		mpd_sendListallCommand(conn,"");

		sp = ls;
		while((sp = strchr(sp,' '))) *sp = '_';

		while((entity = mpd_getNextInfoEntity(conn))) {
			if(entity->type==MPD_INFO_ENTITY_TYPE_DIRECTORY) {
				mpd_Directory * dir = entity->info.directory;
				sp = dp = strdup(dir->path);
				while((sp = strchr(sp,' '))) *sp = '_';
				if(strcmp(dp,ls)==0) {
					free(dp);
					ls = dir->path;
					break;
				}
				free(dp);
			}
			mpd_freeInfoEntity(entity);
		}

		my_finishCommand(conn);

		mpd_sendLsInfoCommand(conn,ls);
		printErrorAndExit(conn);

		while((entity = mpd_getNextInfoEntity(conn))) {
			if(entity->type==MPD_INFO_ENTITY_TYPE_DIRECTORY) {
				mpd_Directory * dir = entity->info.directory;
				printf("%s\n",fromUtf8(dir->path));
			}
			else if(entity->type==MPD_INFO_ENTITY_TYPE_SONG) {
				mpd_Song * song = entity->info.song;
				printf("%s\n",fromUtf8(song->file));
			}
			mpd_freeInfoEntity(entity);
		}

		my_finishCommand(conn);

	} while(++i<argc && (ls = toUtf8(argv[i])));

	return 0;
}

int cmd_lsplaylists ( int argc, char ** argv, mpd_Connection * conn )
{
	mpd_InfoEntity * entity;
	char * ls = "";
	int i = 0;

	if(argc>0) ls = toUtf8(argv[i]);

	do {
		mpd_sendLsInfoCommand(conn,ls);
		printErrorAndExit(conn);

		while((entity = mpd_getNextInfoEntity(conn))) {
			if(entity->type==
					MPD_INFO_ENTITY_TYPE_PLAYLISTFILE) {
				mpd_PlaylistFile * pl = entity->info.playlistFile;
				printf("%s\n",fromUtf8(pl->path));
			}
			mpd_freeInfoEntity(entity);
		}

		my_finishCommand(conn);

	} while(++i<argc && (ls = toUtf8(argv[i])));
	return 0;
}

int cmd_load ( int argc, char ** argv, mpd_Connection * conn )
{
	int i;
	char * sp;
	char * dp;
	mpd_InfoEntity * entity;
	mpd_PlaylistFile * pl;

	for(i=0;i<argc;i++) {
		sp = argv[i];
		while((sp = strchr(sp,' '))) *sp = '_';
	}

	mpd_sendLsInfoCommand(conn,"");
	printErrorAndExit(conn);
	while((entity = mpd_getNextInfoEntity(conn))) {
		if(entity->type==MPD_INFO_ENTITY_TYPE_PLAYLISTFILE) {
			pl = entity->info.playlistFile;
			dp = sp = strdup(fromUtf8(pl->path));
			while((sp = strchr(sp,' '))) *sp = '_';
			for(i=0;i<argc;i++) {
				if(strcmp(dp,argv[i])==0)
					strcpy(argv[i], fromUtf8(pl->path));
			}
			free(dp);
			mpd_freeInfoEntity(entity);
		}
	}
	my_finishCommand(conn);

	mpd_sendCommandListBegin(conn);
	printErrorAndExit(conn);
	for(i=0;i<argc;i++) {
		printf("loading: %s\n",argv[i]);
		mpd_sendLoadCommand(conn,toUtf8(argv[i]));
		printErrorAndExit(conn);
	}
	mpd_sendCommandListEnd(conn);
	my_finishCommand(conn);

	return 0;
}

int cmd_search ( int argc, char ** argv, mpd_Connection * conn ) 
{
	mpd_InfoEntity * entity;
	char * search;
	int table = -1;
	int i;

	struct search_types {
		const char * field;
		const int table;
	} mpc_search [] = {
		{"album", MPD_TABLE_ALBUM },
		{"artist", MPD_TABLE_ARTIST },
		{"title", MPD_TABLE_TITLE },
		{"filename", MPD_TABLE_FILENAME },
		{}
	};

	for(i=0;mpc_search[i].field;++i)
		if (! strcmp(mpc_search[i].field,argv[0]))
			table = mpc_search[i].table;
	if (-1==table) {
		fprintf(stderr,"\"%s\" is not one of: ", argv[0]);
		for(i=0;mpc_search[i].field;++i)
			fprintf(stderr,"%s%s%s",
					( !mpc_search[i+1].field ? "or " : ""),
					mpc_search[i].field,
					(  mpc_search[i+1].field ? ", "   : "\n")
			       );
		return -1;
	}

	for(i=0; i<argc && (search = toUtf8(argv[i])); i++)  {
		mpd_sendSearchCommand(conn,table,search);
		printErrorAndExit(conn);

		while((entity = mpd_getNextInfoEntity(conn))) {
			printErrorAndExit(conn);
			if(entity->type==MPD_INFO_ENTITY_TYPE_DIRECTORY) {
				mpd_Directory * dir = entity->info.directory;
				printf("%s\n",fromUtf8(dir->path));
			}
			else if(entity->type==MPD_INFO_ENTITY_TYPE_SONG) {
				mpd_Song * song = entity->info.song;
				printf("%s\n",fromUtf8(song->file));
			}
			mpd_freeInfoEntity(entity);
		}

		my_finishCommand(conn);
	}
	return 0;
}

int cmd_volume ( int argc, char ** argv, mpd_Connection * conn ) 
{
	int vol;
	int rel = 0;

	if(argc==1) {
		char * test;

		if (0 == strncmp(argv[0],"+",1))
			rel = 1;
		else if (0 == strncmp(argv[0],"-",1))
			rel = -1;

		vol = strtol(argv[0],&test,10);

		if (!rel && (*test!='\0' || vol<0))
			DIE("\"%s\" is not a positive integer\n",argv[0]);
		else if (rel && (*test!='\0'))
			DIE("\"%s\" is not an integer\n", argv[0]);

	} else {
		mpd_Status *status = mpd_getStatus(conn);
		my_finishCommand(conn);

		printf("volume:%3i%c   \n",status->volume,'%');

		mpd_freeStatus(status);

		return 0;
	}

	if (rel)
		mpd_sendVolumeCommand(conn,vol);
	else 
		mpd_sendSetvolCommand(conn,vol);

	my_finishCommand(conn);
	return 1;
}

int cmd_repeat ( int argc, char ** argv, mpd_Connection * conn )
{
	int mode;

	if(argc==1) {
		mode = get_boolean(argv[0]);
		if (mode < 0)
			return -1;
	}
	else {
		mpd_Status * status;
		status = mpd_getStatus(conn);
		my_finishCommand(conn);
		mode = !status->repeat;
		mpd_freeStatus(status);
	}


	mpd_sendRepeatCommand(conn,mode);
	my_finishCommand(conn);

	return 1;
}

int cmd_random ( int argc, char ** argv, mpd_Connection * conn )
{
	int mode;

	if(argc==1) {
		mode = get_boolean(argv[0]);
		if (mode < 0)
			return -1;
	}
	else {
		mpd_Status * status;
		status = mpd_getStatus(conn);
		my_finishCommand(conn);
		mode = !status->random;
		mpd_freeStatus(status);
	}

	mpd_sendRandomCommand(conn,mode);
	my_finishCommand(conn);

	return 1;
}

int cmd_crossfade ( int argc, char ** argv, mpd_Connection * conn )
{
	int seconds;

	if(argc==3) {
		char * test;
		seconds = strtol(argv[0],&test,10);

		if(*test!='\0' || seconds<0)
			DIE("\"%s\" is not 0 or positive integer\n",argv[2]);

		mpd_sendCrossfadeCommand(conn,seconds);
		my_finishCommand(conn);
	}
	else {
		mpd_Status * status = mpd_getStatus(conn);
		my_finishCommand(conn);

		printf("crossfade: %i\n",status->crossfade);

		mpd_freeStatus(status);
	}
	return 0;

}

int cmd_version ( int argc, char ** argv, mpd_Connection * conn )
{
	printf("mpd version: %i.%i.%i\n",conn->version[0],
			conn->version[1],conn->version[2]);
	return 0;
}

int cmd_loadtab ( int argc, char ** argv, mpd_Connection * conn )
{
	mpd_InfoEntity * entity;
	char * sp;
	mpd_PlaylistFile * pl;

	mpd_sendLsInfoCommand(conn,"");
	printErrorAndExit(conn);

	while((entity = mpd_getNextInfoEntity(conn))) {
		if(entity->type==MPD_INFO_ENTITY_TYPE_PLAYLISTFILE) {
			pl = entity->info.playlistFile;
			sp = pl->path;
			while((sp = strchr(sp,' '))) *sp = '_';
			if(argc==1) {
				if(strncmp(pl->path,argv[0],
							strlen(argv[0]))==0) {
					printf("%s\n",
							fromUtf8(pl->path));
				}
			}
		}
		mpd_freeInfoEntity(entity);
	}

	my_finishCommand(conn);
	return 0;
}

int cmd_lstab ( int argc, char ** argv, mpd_Connection * conn )
{
	mpd_InfoEntity * entity;
	char * sp;
	mpd_Directory * dir;

	mpd_sendListallCommand(conn,"");
	printErrorAndExit(conn);

	while((entity = mpd_getNextInfoEntity(conn))) {
		if(entity->type==MPD_INFO_ENTITY_TYPE_DIRECTORY) {
			dir = entity->info.directory;
			sp = dir->path;
			/* replace all ' ' with '_' */
			while((sp = strchr(sp,' '))) *sp = '_';
			if(argc==1) {
				if(strncmp(dir->path,argv[0],
							strlen(argv[0]))==0) {
					printf("%s\n",
							fromUtf8(dir->path));
				}
			}
		}
		mpd_freeInfoEntity(entity);
	}

	my_finishCommand(conn);

	return 0;
}

int cmd_tab ( int argc, char ** argv, mpd_Connection * conn )
{
	mpd_InfoEntity * entity;
	char * sp;
	mpd_Song * song;

	mpd_sendListallCommand(conn,"");
	printErrorAndExit(conn);

	while((entity = mpd_getNextInfoEntity(conn))) {
		if(entity->type==MPD_INFO_ENTITY_TYPE_SONG) {
			song = entity->info.song;
			sp = song->file;
			/* replace all ' ' with '_' */
			while((sp = strchr(sp,' '))) *sp = '_';
			if(argc==1) {
				if(strncmp(song->file,argv[0],
							strlen(argv[0]))==0) {
					printf("%s\n",
							fromUtf8(song->file));
				}
			}
			else printf("%s\n",fromUtf8(song->file));
		}
		mpd_freeInfoEntity(entity);
	}

	my_finishCommand(conn);
	return 0;
}

int cmd_status ( int argc, char ** argv, mpd_Connection * conn )
{
	return 1;
}