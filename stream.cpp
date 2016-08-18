#include "stream.h"
#include "detectobject.h"
#include "recognition.h"


using namespace cv;
using namespace std;

gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data);
GstFlowReturn new_preroll(GstAppSink* asink, gpointer data);
GstFlowReturn new_buffer(GstAppSink* asink, gpointer data);
gboolean timefinished(gpointer data);

detectobject *obj;
recognition *rec;

Ptr<FaceRecognizer> model;

stream::stream()
{
    obj = new detectobject();
    rec = new recognition();
}

GMainLoop *loop;
GstElement *pipeline;
GstElement *appsink;

bool stream::buildpipeline()
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
    if(!gst_element_link_many(src,conv,filter,appsink,NULL))
        return false;


    //assign bus callback
    bus = GST_ELEMENT_BUS(pipeline);
    gst_bus_add_watch(bus, bus_cb, loop);
    gst_object_unref(bus);

    return true;
}

bool stream::trainrecogniser(string name)
{
    qDebug() << __FUNCTION__;

    //set path to image
#ifdef IMX6
    const string filename = "/nvdata/tftpboot/" + name +".png";
#else
    const string filename =  name + ".png";
#endif

    cout << "filename: " << filename << endl;

    //read in data base image
    Mat comp = imread(filename,1);

    //if image read failed return false
    if(comp.empty())
        return false;

    Mat grey(Size(200,200),CV_8UC1);
    //detect channels and apply appropriate conversion
    if(grey.channels() == 3){
        cvtColor(comp,grey,CV_BGR2GRAY);
    }else if(grey.channels() == 4){
        cvtColor(comp,grey, CV_BGRA2GRAY);
    }else{
        grey = comp;
    }

    qDebug() << "comp loaded";

    vector<Mat>faces;
    vector<int>faceLabels;
    faces.push_back(grey);
    faceLabels.push_back(0);

    model = rec->learnCollectedFaces(faces,faceLabels);

    //confirm face rec was set
    string modelName = model->name();
    if(modelName == "")
        return false;

    return true;
}



void stream::startstream()
{
    //set to playing
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

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
static int matches;

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
    t.restart();
    //if face detected
    if(!face.empty()){
        Mat reconstructed = rec->reconstructFace(model, face);
        double similarity = rec->getSimilarity(face,reconstructed);
        if (similarity < 0.6f){
            cout << "match, similarity: " << similarity << endl;
            matches++;
        }else{
            qDebug() << "no match";
        }
        cout << "recognise time: " << t.elapsed() << endl;
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
    cout << "matches : " << matches << endl;
    cout << "framecount: " << framecount << endl;
    g_main_loop_quit(loop);
    return false;
}







