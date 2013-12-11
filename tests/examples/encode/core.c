/*
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 *
 */

#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#ifdef HEADER
#include HEADER
#else
#error "No header found with pipeline definitions"
#endif

/* Were is the recording written to and where is the stream going to */
static void
print_instructions (void)
{
#ifndef NOREC
  g_print ("\nWritting file to:\n");
  g_print ("\t" VDEST "\n");
#endif

  g_print ("\nWritting snapshots to:\n");
  g_print ("\t" SSDEST "\n", 0);

}


/* Need to handle globally because of the signal */
static GstElement *pipeline = NULL;
static void
exit_function (int signo)
{
  if (signo == SIGINT) {
    g_print ("Stopping pipeline...\n");
    g_print ("Sending EOS to close file...");
    gst_element_send_event (pipeline, gst_event_new_eos ());
  }
}

static void
change_caps ()
{
  GstElement *filter;
  GstCaps *caps;
  gint width, height;
  gchar *capsstr;
  filter = gst_bin_get_by_name (GST_BIN (pipeline), "filter");
  if (!filter)
    return;

  width = 320;
  height = 240;

  g_print ("Changing resolution to %dx%d", width, height);
  capsstr = g_strdup_printf ("video/x-raw, width=(int)%d, height=(int)%d",
      width, height);
  caps = gst_caps_from_string (capsstr);
  g_free (capsstr);
  g_object_set (filter, "caps", caps, NULL);
  gst_caps_unref (caps);

}

/* Wait for user ENTER key presses */
static void *
user_input_func (void *data)
{
  gboolean *trigger = (gboolean *) data;
  int c;
  do {
    c = getchar ();
    if (c == 'c') {
      g_print ("Change caps ... \n");
      change_caps ();
    } else if (c == 's') {
      *trigger = TRUE;
    }
  } while (TRUE);

  return NULL;
}

/* This function is used to display the performance of the pipeline
   and to catch the EOS to know its safe to close the pipeline */
static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (message)) {

    case GST_MESSAGE_INFO:{
      gchar *performance = NULL;
      GError *error = NULL;
      gst_message_parse_info (message, &error, &performance);
      g_print ("%s\n", performance);
      g_free (performance);
      g_error_free (error);
      break;
    }

    case GST_MESSAGE_EOS:{
      g_print ("done!\n");
      g_main_loop_quit (loop);
      break;
    }

    default:
      break;
  }

  return TRUE;
}

/*This function will be called whenever a buffer passes through the
  snapshot branch */
static gboolean
snapshot_probe (GstPad * pad, GstBuffer * buffer, gpointer data)
{
  gboolean *trigger = (gboolean *) data;

  if (*trigger) {
    *trigger = FALSE;
    g_print ("Snapshot taken!\n");
    return TRUE;
  }

  return FALSE;
}

int
main (int argc, char **argv)
{
  GstElement *snapshot = NULL;
  pthread_t user_input;
  GstBus *bus = NULL;
  GMainLoop *loop = NULL;
  GError *error = NULL;
  gboolean trigger = FALSE;
  int ret = EXIT_SUCCESS;

  gst_init (&argc, &argv);

  pipeline = gst_parse_launch (PIPE, &error);
  if (!pipeline) {
    g_warning ("Unable to construct pipeline: %s\n", error->message);
    //    ret = EXIT_FAILURE;
    goto exit;
  } else {
    g_print ("Pipeline successfully created\n");
  }

  loop = g_main_loop_new (NULL, FALSE);
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_callback, (gpointer) loop);
  gst_object_unref (bus);


  snapshot = gst_bin_get_by_name (GST_BIN (pipeline), "snapshot");
  if (!snapshot) {
    g_warning ("Unable to identify snapshot branch to place buffer probe\n");
    ret = EXIT_FAILURE;
    goto exit;
  } else {
    GstPad *pad = gst_element_get_static_pad (snapshot, "sink");

    g_print ("Identified snapshot branch, placing probe\n");
    gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
        (GstPadProbeCallback) snapshot_probe, (gpointer) & trigger, NULL);
    gst_object_unref (pad);
  }

  g_print ("Playing pipeline... %s\n", PIPE);
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (pipeline,
          GST_STATE_PLAYING)) {
    g_warning ("Unable to play pipeline\n");
    ret = EXIT_FAILURE;
    goto exit;
  }

  /* Dump a drawing of the pipeline */
  const gchar *dot = g_strdup_printf ("%s", argv[0]);
  GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE |
      GST_DEBUG_GRAPH_SHOW_CAPS_DETAILS |
      GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS, dot);
  g_free ((gpointer) dot);

  /* Connect to the outside world */
  pthread_create (&user_input, NULL, user_input_func, (void *) &trigger);
  signal (SIGINT, exit_function);
#if 1
  g_print ("Testing pipeline state changes\n");
  sleep (5);
  g_print ("Changing to ready\n");
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (pipeline,
          GST_STATE_READY)) {
    g_warning ("Unable to pause pipeline\n");
    ret = EXIT_FAILURE;
    goto exit;
  }

  sleep (5);
  g_print ("Changing to playing\n");
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (pipeline,
          GST_STATE_PLAYING)) {
    g_warning ("Unable to pause pipeline\n");
    ret = EXIT_FAILURE;
    goto exit;
  }
#endif
  g_print ("Running\n");
  print_instructions ();
  g_print ("\n\tPress ENTER to take a snapshot\n");
  g_print ("\tPress CTRL+C to exit\n\n");
  g_main_loop_run (loop);

  g_print ("Closing pipeline and flushing cache...\n");
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (pipeline,
          GST_STATE_NULL)) {
    g_warning ("Unable to stop pipeline\n");
    ret = EXIT_FAILURE;
    goto exit;
  }
#if 1
  g_print ("\nPlaying pipeline again... %s\n", PIPE);
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (pipeline,
          GST_STATE_PLAYING)) {
    g_warning ("Unable to play pipeline\n");
    ret = EXIT_FAILURE;
    goto exit;
  }

  print_instructions ();
  g_print ("\n\tPress ENTER to take a snapshot\n");
  g_print ("\tPress CTRL+C to exit\n\n");

  g_main_loop_run (loop);

  g_print ("Closing pipeline and flushing cache...");
  if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (pipeline,
          GST_STATE_NULL)) {
    g_warning ("Unable to stop pipeline\n");
    ret = EXIT_FAILURE;
    goto exit;
  }
#endif
  g_print ("done!\n");

exit:
  if (snapshot)
    gst_object_unref (snapshot);
  if (loop)
    g_main_loop_unref (loop);
  if (pipeline)
    gst_object_unref (pipeline);

  return ret;
}
