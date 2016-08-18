#include "stream.h"
#include "detectobject.h"
#include "recognition.h"
#include <vector>
#include <QTime>

using namespace cv;
using namespace std;

gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data);
GstFlowReturn new_preroll(GstAppSink* asink, gpointer data);
GstFlowReturn new_buffer(GstAppSink* asink, gpointer data);
gboolean timefinished(gpointer data);

detectobject *obj;
recognition *rec;

stream::stream()
{
    obj = new detectobject();
    rec = new recognition();
}

GMainLoop *loop;
GstElement *pipeline;
GstElement *appsink;

void stream::buildpipeline()
{
    qDebug() << __FUNCTION__;

    // gst-launch-0.10 v4l2src ! ffmpegcolorspace ! video/x-raw-rgb, width=640, height=480 ! ximagesink

    //create required componenets
    GstElement *src;
    GstElement *filter;
    GstElement *conv;
    GstBus *bus;

    loop = g_main_loop_new(NULL,false);

    pipeline = gst_pipeline_new(NULL);
#ifdef IMX6
    src = gst_element_factory_make("mfw_v4lsrc", NULL);
#else
    src = gst_element_factory_make("v4l2src", NULL);
#endif
    conv = gst_element_factory_make("ffmpegcolorspace", NULL);
    filter = gst_element_factory_make("capsfilter", NULL);
    gst_util_set_object_arg(G_OBJECT(filter), "caps",
                            "video/x-raw-gray, width=640, height=480, bpp=8, depth=8, framerate=(fraction)20/1");

    appsink = gst_element_factory_make("appsink", NULL);

    //set app sink properties
    gst_app_sink_set_emit_signals((GstAppSink*)appsink, true);
    gst_app_sink_set_drop((GstAppSink*)appsink, true);
    gst_app_sink_set_max_buffers((GstAppSink*)appsink, 1);
    GstAppSinkCallbacks callbacks = {NULL, new_preroll, new_buffer, NULL};
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, NULL, NULL);

    //add components to pipelines
    gst_bin_add_many(GST_BIN(pipeline), src,conv, filter, appsink, NULL);

    //link
    gst_element_link_many(src,conv,filter,appsink,NULL);

    //set to playing
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    //assign bus callback
    bus = GST_ELEMENT_BUS(pipeline);
    gst_bus_add_watch(bus, bus_cb, loop);
    gst_object_unref(bus);

    //set program timeout function
    g_timeout_add(5000,timefinished,NULL);

    //run loop
    g_main_loop_run(loop);

    //once exited, dispose of elements
    gst_element_set_state(pipeline, GST_STATE_NULL);

    gst_object_unref(pipeline);

    QCoreApplication::instance()->quit();

    return;
}

gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data)
{
    Q_UNUSED(bus);
    Q_UNUSED(user_data);
    //parse bus messages
    switch(GST_MESSAGE_TYPE(msg)){
        case GST_MESSAGE_ERROR:{
            //quit on error
            GError *err;
            gchar *dbg;
            gst_message_parse_error(msg, &err, &dbg);
            gst_object_default_error(msg->src, err, dbg);
            g_clear_error(&err);
            g_free(dbg);
            g_main_loop_quit(loop);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED:{
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
            //just show pipeline messages
            if(GST_OBJECT_NAME(msg->src) == GST_OBJECT_NAME(pipeline)){
                g_print("'%s' state changed from %s to %s \n", GST_OBJECT_NAME(msg->src), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
            }
            break;
        }
        default:
            break;
    }
    return TRUE;
}

GstFlowReturn new_preroll(GstAppSink* asink, gpointer data)
{
    //confirms successful initialisation
    Q_UNUSED(asink);
    Q_UNUSED(data);
    g_print("Got preroll\n");
    return GST_FLOW_OK;
}

static int framecount;

GstFlowReturn new_buffer(GstAppSink* asink, gpointer data)
{
    framecount++;
    //discard initial frames as camera takes a few frames to apply all setting properly
    if(framecount <= 10){
        return GST_FLOW_OK;
    }

    //emit signals false - will not enter this function again until set to true
    gst_app_sink_set_emit_signals((GstAppSink*)asink, false);

    //debug timer
    QTime t;
    t.start();

    Q_UNUSED(data);
    char filename[16];

    //grab available buffer from appsink
    GstBuffer *buf = gst_app_sink_pull_buffer(asink);

    if(framecount == 1){
        cout << "size: " <<  GST_BUFFER_SIZE(buf) << endl;
    }

    //allocate buffer data to cvMat format
    Mat frame(Size(640,480), CV_8UC1, GST_BUFFER_DATA(buf), Mat::AUTO_STEP);

    //confirm not empty
    if(frame.empty())
        return GST_FLOW_ERROR;

    int begindetect = t.elapsed();

    //send captured frame through face detection
    Mat face = obj->findFace(frame);
    int detect = t.elapsed() - begindetect;
    cout << "detect time: " << detect << endl;

    //if face detected
    if(!face.empty()){
        //set image write params
        vector<int> compression_params;
        compression_params.push_back(CV_IMWRITE_PNG_COMPRESSION);
        compression_params.push_back(9);

    #ifdef IMX6
        sprintf(filename, "/nvdata/tftpboot/brandon%d.png", framecount);
    #else
        sprintf(filename, "./brandon%d.png", framecount);
    #endif
        imwrite(filename, face, compression_params);
        qDebug() << "image captured";

    }
    t.restart();

    //face detection finished - emit signals will notify when next buffer is available
    gst_app_sink_set_emit_signals((GstAppSink*)asink, true);
    return GST_FLOW_OK;
}

gboolean timefinished(gpointer data)
{
    Q_UNUSED(data);
    qDebug() << __FUNCTION__;
    g_main_loop_quit(loop);
    return false;
}







