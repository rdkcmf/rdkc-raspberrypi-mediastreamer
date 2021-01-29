/**
##########################################################################
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2019 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################
**/

/**
# This file contains code written by Sergey Lyubka.
# Copyright (c) 2004-2009 Sergey Lyubka
# Licensed under the MIT license
**/

/************* Included Header FIle **************/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <mongoose.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/base/gstbasesrc.h>

/*RDK Logging */
#include "rdk_debug.h"
#include <sys/wait.h>
#include <unistd.h>    /* For pause() */

/************* Define / Macros ***************/
#define DIRSEP        '/'
#define	CONFIG_FILE   "mongoose.conf"

#define URI_SIZE 200

/************ Static / Glocal variable ****************/
static int    exit_flag;        /* Program termination flag	*/
static struct mg_context *ctx;  /* Mongoose context		*/

int video_capture_started = 0;

GstElement *pstcamerasrc = NULL;

typedef enum camerasrc
{
    CAMERA_V4L2_SRC = 1,
    CAMERA_LIBCAMERA_SRC
}CAMERA_SRC;

typedef struct mediastreamer
{
    char avideotype[50];
    char aformat[20];

    int width;
    int height;
    int framerate;
    int do_timestamp;

}MEDIASTREAMER;

MEDIASTREAMER stMediastreamer = { 0 };

CAMERA_SRC encamerasrc = CAMERA_V4L2_SRC;

/************ Prototype *************/

void load_default_mediastreamer_value();

extern int mg_stat(const char *path, struct mgstat *stp);

int main(int argc, char *argv[]);

static void signal_handler(int sig_num);

void parse_and_start(struct mg_connection *conn,const struct mg_request_info *request_info,void *user_data);

void start_stream(struct mg_connection *conn );

void * on_new_sample(GstElement *elt,struct mg_connection *conn);

void * on_new_sample_with_timestamp_and_size(GstElement *elt,struct mg_connection *conn);

gboolean on_message(GstBus *bus,GstMessage *message, gpointer userData);

void parse_and_stop(struct mg_connection *conn,const struct mg_request_info *request_info,void *user_data);

void parse_and_set(struct mg_connection *conn,const struct mg_request_info *request_info,void *user_data);

void LoadAttributesValue( char *pgetstream, char *pAttributes, char *pValue);

void parse_and_get(struct mg_connection *conn,const struct mg_request_info *request_info,void *user_data);


/************* API *****************/

/* load_default_mediastreamer_value() */
void load_default_mediastreamer_value()
{
    memset( &stMediastreamer, 0, sizeof( stMediastreamer ) );

    stMediastreamer.width = 640;
    stMediastreamer.height = 480;

    stMediastreamer.framerate  = 30;

    strcpy( stMediastreamer.avideotype, "video/x-raw" );
}
/* }}} */

/* {{{ main() */
int main(int argc, char *argv[])
{
    /* RDK logger initialization */
    rdk_logger_init("/etc/debug.ini");
 
    gst_init(&argc,&argv);

    (void) signal(SIGCHLD, signal_handler);

    (void) signal(SIGTERM, signal_handler);

    (void) signal(SIGINT, signal_handler);
	
    load_default_mediastreamer_value();

    if( NULL != argv[1] )
    {
        if( 0 == strcmp( argv[1], "v4l2src") )
        {
            encamerasrc = CAMERA_V4L2_SRC;
        }
        else if( 0 == strcmp( argv[1], "libcamerasrc") )
        {
            encamerasrc = CAMERA_LIBCAMERA_SRC;
        }
    }

    if ((ctx = mg_start()) == NULL) 
    {
        RDK_LOG( RDK_LOG_ERROR,"LOG.RDK.GSTREAMER","%s(%d): Cannot initialialize Mongoose context\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

    if (mg_get_option(ctx, "ports") == NULL &&
        mg_set_option(ctx, "ports", "8080") != 1)
        exit(EXIT_FAILURE);

    mg_bind_to_uri( ctx, "/setstream*", &parse_and_set, NULL);
    mg_bind_to_uri( ctx, "/getstream*", &parse_and_get, NULL);
    mg_bind_to_uri( ctx, "/startstream*", &parse_and_start, NULL);
    mg_bind_to_uri( ctx, "/stopstream*", &parse_and_stop, NULL);

    RDK_LOG( RDK_LOG_INFO,"LOG.RDK.GSTREAMER","%s(%d): Mongoose %s started on port(s) [%s], serving directory [%s]\n", __FILE__, __LINE__,
			
    mg_version(),
    mg_get_option(ctx, "ports"),
    mg_get_option(ctx, "root"));

    fflush(stdout);

    while (exit_flag == 0)
        sleep(1);

    RDK_LOG( RDK_LOG_INFO,"LOG.RDK.GSTREAMER","%s(%d): Exiting on signal %d waiting for all threads to finish...",__FILE__, __LINE__,exit_flag);

    fflush(stdout);

    mg_stop(ctx);

    RDK_LOG( RDK_LOG_INFO,"LOG.RDK.GSTREAMER","%s(%d): done.\n", __FILE__, __LINE__);

    return (EXIT_SUCCESS);
}
/* }}} */

/* {{{ signal_handler() */
static void signal_handler(int sig_num)
{
    if (sig_num == SIGCHLD) 
    {
        do 
        {
        } while (waitpid(-1, &sig_num, WNOHANG) > 0);
    }
    else
    {
        exit_flag = sig_num;
    }
}
/* }}} */

/* {{{ parse_and_start() */
void parse_and_start(struct mg_connection *conn,const struct mg_request_info *request_info,void *user_data)
{
    memset( stMediastreamer.aformat, 0, sizeof( stMediastreamer.aformat ) );

    parse_and_set(conn,request_info,user_data);

    start_stream(conn);

    return;
}
/* }}} */

/* {{{ start_stream() */
void start_stream(struct mg_connection *conn )
{
    GstElement *pipeline,*camerasrc,*filter,*videoconvert,*h264enc,*appsink;
    GMainLoop  *loop;
    GstBus     *bus;
    GstCaps    *filtercaps;

    (void) mg_printf(conn,
           "HTTP/1.1 %d %s\r\n"
           "Content-Type: %s\r\n"
           "Connection: %s\r\n"
           "Accept-Ranges: bytes\r\n"
           "%s\r\n",
           200, "OK", "video/mpeg","close","\0");
	
    if( video_capture_started )
    {
        RDK_LOG( RDK_LOG_DEBUG,"LOG.RDK.GSTREAMER","%s(%d):Video capture going on \n", __FILE__, __LINE__);
        return;
    }

    pipeline = gst_element_factory_make("pipeline","pipeline");

    filter = gst_element_factory_make("capsfilter","filter");

    appsink = gst_element_factory_make("appsink","appsink");

    if( CAMERA_V4L2_SRC == encamerasrc )
    {
        pstcamerasrc = camerasrc = gst_element_factory_make("v4l2src","v4l2src");

        h264enc = gst_element_factory_make("omxh264enc","omxh264enc");	

        if ( !pipeline || !camerasrc || !filter || !h264enc || !appsink )
        {
            RDK_LOG( RDK_LOG_ERROR,"LOG.RDK.GSTREAMER","%s(%d): Unable to make elements\n", __FILE__, __LINE__);
        }
    }
    else if( CAMERA_LIBCAMERA_SRC == encamerasrc )
    {
        pstcamerasrc = camerasrc = gst_element_factory_make("libcamerasrc","libcamerasrc");

	videoconvert = gst_element_factory_make("videoconvert","videoconvert");

	h264enc = gst_element_factory_make("v4l2h264enc","v4l2h264enc");

	if ( !pipeline || !camerasrc || !filter || !videoconvert || !h264enc || !appsink )
        {
            RDK_LOG( RDK_LOG_ERROR,"LOG.RDK.GSTREAMER","%s(%d): Unable to make elements\n", __FILE__, __LINE__);
        }
    }

    g_object_set(G_OBJECT(appsink),"emit-signals",TRUE,"sync",FALSE,NULL);

    if( stMediastreamer.do_timestamp )
    {
        g_signal_connect(appsink,"new-sample",G_CALLBACK(on_new_sample_with_timestamp_and_size),conn);
    }
    else
    {
        g_signal_connect(appsink,"new-sample",G_CALLBACK(on_new_sample),conn);
    }

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

    loop = g_main_loop_new(NULL,FALSE);

    gst_bus_add_watch(bus,(GstBusFunc) on_message, loop);

    if( 0 == strcmp( stMediastreamer.aformat, "") )
    {
	if( CAMERA_V4L2_SRC == encamerasrc )
	{
            gst_bin_add_many(GST_BIN(pipeline),camerasrc,filter,h264enc,appsink,NULL);

            if ( gst_element_link_many(camerasrc,filter,h264enc,appsink,NULL) )
            {
                 RDK_LOG( RDK_LOG_DEBUG,"LOG.RDK.GSTREAMER","%s(%d):Element linking success for pipeline\n", __FILE__, __LINE__);

            } else
            {
                RDK_LOG( RDK_LOG_ERROR,"LOG.RDK.GSTREAMER","%s(%d):Element linking failure for pipeline\n", __FILE__, __LINE__);

            }

             filtercaps = gst_caps_new_simple (stMediastreamer.avideotype,
                                      "width", G_TYPE_INT, stMediastreamer.width,
                                      "height", G_TYPE_INT, stMediastreamer.height,
                                      "framerate", GST_TYPE_FRACTION, stMediastreamer.framerate, 1,
                                      NULL);
	}
        else if( CAMERA_LIBCAMERA_SRC == encamerasrc )	
	{
            gst_bin_add_many(GST_BIN(pipeline),camerasrc,filter,videoconvert,h264enc,appsink,NULL);

            if ( gst_element_link_many(camerasrc,filter,videoconvert,h264enc,appsink,NULL) )
            {
                 RDK_LOG( RDK_LOG_DEBUG,"LOG.RDK.GSTREAMER","%s(%d):Element linking success for pipeline\n", __FILE__, __LINE__);

            } else
            {
                RDK_LOG( RDK_LOG_ERROR,"LOG.RDK.GSTREAMER","%s(%d):Element linking failure for pipeline\n", __FILE__, __LINE__);

            }

             filtercaps = gst_caps_new_simple (stMediastreamer.avideotype,
                                      "width", G_TYPE_INT, stMediastreamer.width,
                                      "height", G_TYPE_INT, stMediastreamer.height,
                                      "framerate", GST_TYPE_FRACTION, stMediastreamer.framerate, 1,
				      "format", G_TYPE_STRING, "NV12",
                                      NULL);
	}	
    }
    else
    {
        gst_bin_add_many(GST_BIN(pipeline),camerasrc,filter,appsink,NULL);

        if ( gst_element_link_many(camerasrc,filter,appsink,NULL) )
        {
            RDK_LOG( RDK_LOG_DEBUG,"LOG.RDK.GSTREAMER","%s(%d):Element linking success for pipeline\n", __FILE__, __LINE__);

        } else
        {
            RDK_LOG( RDK_LOG_ERROR,"LOG.RDK.GSTREAMER","%s(%d):Element linking failure for pipeline\n", __FILE__, __LINE__);

        }

        filtercaps = gst_caps_new_simple (stMediastreamer.avideotype,
                                      "width", G_TYPE_INT, stMediastreamer.width,
                                      "height", G_TYPE_INT, stMediastreamer.height,
                                      "framerate", GST_TYPE_FRACTION, stMediastreamer.framerate, 1,
				      "format", G_TYPE_STRING, stMediastreamer.aformat,
                                      NULL);

    }
	
    g_object_set (G_OBJECT (filter), "caps", filtercaps, NULL);
	
    gst_caps_unref (filtercaps);

    video_capture_started = 1;
	
    gst_element_set_state(pipeline,GST_STATE_PLAYING);

    if( gst_element_get_state(pipeline,NULL,NULL,GST_CLOCK_TIME_NONE) == GST_STATE_CHANGE_SUCCESS)
    {
        RDK_LOG( RDK_LOG_DEBUG,"LOG.RDK.GSTREAMER","%s(%d):The state of pipeline changed to GST_STATE_PLAYING successfully\n", __FILE__, __LINE__);

    } else
    {
        RDK_LOG( RDK_LOG_ERROR,"LOG.RDK.GSTREAMER","%s(%d):The state of pipeline changed to GST_STATE_PLAYING failed\n", __FILE__, __LINE__);
    }

    g_main_loop_run(loop);

    gst_element_set_state(appsink,GST_STATE_READY);

    gst_element_set_state(filter,GST_STATE_READY);

    gst_element_set_state(videoconvert,GST_STATE_READY);

    gst_element_set_state(h264enc,GST_STATE_READY);

    gst_element_set_state(camerasrc,GST_STATE_READY);


    gst_element_set_state(appsink,GST_STATE_NULL);

    gst_element_set_state(filter,GST_STATE_READY);

    gst_element_set_state(videoconvert,GST_STATE_READY);

    gst_element_set_state(h264enc,GST_STATE_NULL);

    gst_element_set_state(camerasrc,GST_STATE_NULL);


    gst_element_set_state(pipeline,GST_STATE_NULL);


    if( gst_element_get_state(pipeline,NULL,NULL,GST_CLOCK_TIME_NONE) == GST_STATE_CHANGE_SUCCESS)
    {
        RDK_LOG( RDK_LOG_DEBUG,"LOG.RDK.GSTREAMER","%s(%d):The state of pipeline changed to GST_STATE_NULL successfully\n", __FILE__, __LINE__);

    } else
    {
        RDK_LOG( RDK_LOG_ERROR,"LOG.RDK.GSTREAMER","%s(%d):Changing the state of pipeline to GST_STATE_NULL failed\n", __FILE__, __LINE__);

    }

    if( 0 == strcmp( stMediastreamer.aformat, "") )
    {
        if( CAMERA_V4L2_SRC == encamerasrc)
        {
            gst_element_unlink_many(camerasrc,filter,h264enc,appsink,NULL);

            gst_bin_remove_many(GST_BIN(pipeline),appsink,h264enc,filter,camerasrc,NULL);
	}
	else if( CAMERA_LIBCAMERA_SRC == encamerasrc)
        {
            gst_element_unlink_many(camerasrc,filter,videoconvert,h264enc,appsink,NULL);

            gst_bin_remove_many(GST_BIN(pipeline),appsink,h264enc,videoconvert,filter,camerasrc,NULL);
        }
    }
    else
    {
        gst_element_unlink_many(camerasrc,filter,appsink,NULL);

        gst_bin_remove_many(GST_BIN(pipeline),appsink,filter,camerasrc,NULL);
    }

    gst_object_ref(camerasrc);

    gst_object_unref(pipeline);

    RDK_LOG( RDK_LOG_DEBUG,"LOG.RDK.GSTREAMER","%s(%d):pipeline deleted\n", __FILE__, __LINE__);

}
/* }}} */

/* {{{ on_new_sample() */
void * on_new_sample(GstElement *elt,struct mg_connection *conn)
{	
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo map;

    sample = gst_app_sink_pull_sample(GST_APP_SINK(elt));

    buffer = gst_sample_get_buffer(sample);

    gst_buffer_map(buffer,&map,GST_MAP_READ);

    mg_write(conn, map.data, map.size);

    gst_buffer_unmap(buffer,&map);

    gst_sample_unref(sample);

    return NULL;

}
/* }}} */

/* {{{ on_new_sample_with_timestamp_and_size)_ */
void * on_new_sample_with_timestamp_and_size(GstElement *elt,struct mg_connection *conn)
{
    GstSample   *sample;
    GstBuffer   *buffer;
    GstMapInfo  map;
    guint64     timestamp;
    gsize       buffersize;
    GstClock   *gstclock;
    
    sample = gst_app_sink_pull_sample(GST_APP_SINK(elt));

    buffer = gst_sample_get_buffer(sample);

    gst_buffer_map(buffer,&map,GST_MAP_READ);

    gstclock =gst_system_clock_obtain();
    g_object_set(G_OBJECT(gstclock),"clock-type",GST_CLOCK_TYPE_REALTIME,NULL);
    
    timestamp = GST_BUFFER_TIMESTAMP(buffer);
    buffersize = map.size;
    
    mg_write(conn,&timestamp,sizeof(timestamp));
    mg_write(conn,&buffersize,sizeof(buffersize));
    mg_write(conn, map.data, map.size);
    
    gst_object_unref(GST_OBJECT(gstclock));
    gst_buffer_unmap(buffer,&map);
    gst_sample_unref(sample);
    
	return NULL;
}
/* }}} */

/* {{{ on_message() */
gboolean on_message(GstBus *bus,GstMessage *message, gpointer userData)
{
    GMainLoop *loop = (GMainLoop *) userData;

    switch(GST_MESSAGE_TYPE(message))
    {
        case GST_MESSAGE_EOS:
        {
            RDK_LOG( RDK_LOG_DEBUG,"LOG.RDK.GSTREAMER","%s(%d): EOS has reached\n", __FILE__, __LINE__);

            g_main_loop_quit(loop);
        }
            break;

        case GST_MESSAGE_ERROR:
        {
            RDK_LOG( RDK_LOG_ERROR,"LOG.RDK.GSTREAMER","%s(%d): Error has occured\n", __FILE__, __LINE__);

            g_main_loop_quit(loop);
        }
            break;

        default:
            break;
    }

    return TRUE;

}
/* }}} */

/* {{{ parse_and_stop() */
void parse_and_stop(struct mg_connection *conn,const struct mg_request_info *request_info,void *user_data)
{	
    if( video_capture_started )
    {
        video_capture_started = 0;

        gst_element_send_event(pstcamerasrc, gst_event_new_eos());

        mg_printf(conn, "%s","HTTP/1.1 200 OK\r\n");
    }

}
/* }}} */

/* {{{ parse_and_set() */
void parse_and_set(struct mg_connection *conn,const struct mg_request_info *request_info,void *user_data)
{
    if( NULL != request_info )
    {
        char auri[URI_SIZE] = { 0 };

        strcpy( auri , request_info->uri );

        char *pacText            = NULL,
             *pacCheckAttributes = NULL,
             *pacSetValue        = NULL;

        pacText = strtok( auri , "&" );

        while( pacText != NULL )
        {
            pacText = strtok( NULL , "&" );

            pacCheckAttributes = strtok_r( pacText, "=", &pacSetValue );

            if( strcmp( pacCheckAttributes, "width") == 0 )
            {
                stMediastreamer.width = atoi( pacSetValue );
            }
	
            if( strcmp( pacCheckAttributes, "height" ) == 0 )
            {
                stMediastreamer.height = atoi( pacSetValue );
            }
			
            if( strcmp( pacCheckAttributes, "framerate" ) == 0 )
            {
                stMediastreamer.framerate = atoi( pacSetValue );
            }

            if( strcmp( pacCheckAttributes, "videotype" ) == 0 )
            {
                strcpy( stMediastreamer.avideotype , pacSetValue );
            }

            if( strcmp( pacCheckAttributes, "do_timestamp" ) == 0 )
            {
                stMediastreamer.do_timestamp = atoi( pacSetValue );
            }

            if( strcmp( pacCheckAttributes, "format" ) == 0 )
            {
                strcpy( stMediastreamer.aformat , pacSetValue );
	    }
        }
		
    }
	mg_printf(conn, "%s","HTTP/1.1 200 OK\r\n");
}
/* }}} */

/* {{{ LoadAttributesValue */
void LoadAttributesValue( char *pgetstream, char *pAttributes, char *pValue)
{
    if( ( NULL != pgetstream ) && ( NULL != pAttributes ) && ( NULL != pValue ) )
    {
        strcat( pgetstream , pAttributes);
        strcat( pgetstream, "=");
        strcat( pgetstream,pValue);
        strcat( pgetstream,"&");
    }
}
/* }}} */

/* {{{ parse_and_get() */
void parse_and_get(struct mg_connection *conn,const struct mg_request_info *request_info,void *user_data)
{
    if( NULL != request_info )
    {
        char getstream[URI_SIZE] = { 0 },
             auri[URI_SIZE]      = { 0 },
             aValue[10]     = { 0 };

        strcpy( auri , request_info->uri );

        char *pacText = NULL, 
             *pacCheckAttributes = NULL,
             *pacSetValue        = NULL;

        pacText = strtok( auri , "&" );

        while( pacText != NULL )
        {
            pacText = strtok( NULL , "&" );

            pacCheckAttributes = strtok_r( pacText, "=", &pacSetValue );
			
            if( strcmp( pacSetValue, "**" ) == 0 )
            {	
                if( strcmp( pacCheckAttributes, "width") == 0 )
                {
                    sprintf(aValue,"%d",stMediastreamer.width );
                    LoadAttributesValue( getstream, pacCheckAttributes, aValue );
            	}

                if( strcmp( pacCheckAttributes, "height") == 0 )
                {
                    sprintf(aValue,"%d",stMediastreamer.height );
                    LoadAttributesValue( getstream, pacCheckAttributes, aValue );
                }

                if( strcmp( pacCheckAttributes, "framerate") == 0 )
                {
                    sprintf(aValue,"%d",stMediastreamer.framerate );
                    LoadAttributesValue( getstream, pacCheckAttributes, aValue );
                }

                if( strcmp( pacCheckAttributes, "videotype") == 0 )
                {
                    sprintf(aValue,"%s",stMediastreamer.avideotype );
                    LoadAttributesValue( getstream, pacCheckAttributes, aValue );
                }

                if( strcmp( pacCheckAttributes, "do_timestamp") == 0 )
                {
                    sprintf(aValue,"%d",stMediastreamer.do_timestamp );
                    LoadAttributesValue( getstream, pacCheckAttributes, aValue );
                }

                if( strcmp( pacCheckAttributes, "format") == 0 )
                {
                    sprintf(aValue,"%s",stMediastreamer.aformat );
                    LoadAttributesValue( getstream, pacCheckAttributes, aValue );
                }
            }
        }

        mg_printf(conn, "%s","HTTP/1.1 200 OK\r\nContent-Type:text/html\r\n\r\n");	
        mg_printf(conn,"%s",getstream);

        strcpy(getstream,"http:/getstream&");
    }
}
/* }}} */
