#include "stream.h"
#include "detectobject.h"
#include "recognition.h"


using namespace cv;
using namespace std;

gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data);
GstFlowReturn new_preroll(GstAppSink* asink, gpointer data);
GstFlowReturn new_buffer(GstAppSink* asink, gpointer data);
gboolean timefinished(gpointer data);
void writeimage(Mat image);

const char *facerecAlgorithm = "FaceRecognizer.Eigenfaces";

detectobject *obj;
recognition *rec;

//set detection and match thresholds
#ifdef IMX6
    const double SIMILARITY_THRESH = 0.2f;
    const int MATCH_THRESHOLD = 3;
    const int CONSECUTIVE_THRESHOLD = 2;
#else
    const double SIMILARITY_THRESH = 0.22f;
    const int MATCH_THRESHOLD = 30;
#endif

static int framecount;
static int matches;
static int consecutive;

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


    vector<Mat>faces;
    vector<int>labels;
    //3 images per user
    for(int i = 0; i <=2; i++){
    #ifdef IMX6
        string username = "/nvdata/tftpboot/" +name + format("%d.png",i);
#else
        string username = name + format("%d.png",i);
#endif
        cout << username << endl;
        Mat user = imread(username,1);
        if(user.empty()){
            qDebug() << "image read failed";
            return false;
        }

        Mat grey;

        //detect channels and apply appropriate conversion
        if(user.channels() == 3){
            cvtColor(user,grey,CV_BGR2GRAY);
        }else if(user.channels() == 4){
            cvtColor(user,grey, CV_BGRA2GRAY);
        }else{
            grey = user;
        }

        //crop edges of mask
        Rect ROI;
        ROI.x = 20;
        ROI.width = grey.cols - 40;
        ROI.y = 0;
        ROI.height = grey.rows;

        Mat final = grey(ROI);

        //facerecogniser needs square images
        resize(final,final,Size(160,160));

        //add each face to array
        faces.push_back(final);
        labels.push_back(0);
    }
    ///train the recogniser on the database images
    model = rec->learnCollectedFaces(faces,labels,facerecAlgorithm);

    //confirm face rec was set
    string modelName = model->name();
    if(modelName != facerecAlgorithm)
        return false;

    return true;
}

void stream::startstream()
{
    qDebug() << __FUNCTION__;

    //set to playing
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    //set program timeout function
    g_timeout_add(5000,timefinished,NULL);

    //run loop
    g_main_loop_run(loop);

    //once exited, dispose of elements
    gst_element_set_state(pipeline, GST_STATE_NULL);

    gst_object_unref(pipeline);

    if(matches < MATCH_THRESHOLD && consecutive < CONSECUTIVE_THRESHOLD){
        qDebug() << "***Access Denied***";
    }

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


GstFlowReturn new_buffer(GstAppSink* asink, gpointer data)
{
    vector<Mat> matchfaces;
    vector<int>labels;
    framecount++;

    //discard initial frames as camera takes a few frames to apply all setting properly
    if(framecount <= 5){
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
        //run captured image through trained recogniser
        Mat reconstructed = rec->reconstructFace(model, face);        
        //compare captured image with average image from database
        double similarity = rec->getSimilarity(reconstructed,face);

//for debugging on board
#ifdef IMX6
        //writeimage(face);
#endif
        if (similarity < SIMILARITY_THRESH){
            int id = model->predict(face);
            //if face is a match
            if(id == 0){
                cout << "match, similarity: " << similarity << endl;
                matches++;
                consecutive++;
                //retrain the recogniser with match - improves subsequent matches
                matchfaces.push_back(face);
                labels.push_back(id);
                model->train(matchfaces,labels);
            }else{
                qDebug() << "unexpected user";
                cout << "similarity: " << similarity << endl;
            }
        }else{
            cout << "no user detected, similarity: " << similarity << endl;
            consecutive = 0;
        }        
    }else{
        qDebug() << "No face found";
        consecutive = 0;
    }
    t.restart();

    if(matches >= MATCH_THRESHOLD || consecutive >= CONSECUTIVE_THRESHOLD){
        qDebug() << "***Access Granted***";
        timefinished(NULL);
    }

    //face detection finished - emit signals will notify when next buffer is available
    gst_app_sink_set_emit_signals((GstAppSink*)asink, true);
    return GST_FLOW_OK;
}


int cap;
void writeimage(Mat image)
{
    //write captured image to file
    char filename[16];
    cap++;
    vector<int>compression_params;
    compression_params.push_back(CV_IMWRITE_PNG_COMPRESSION);
    compression_params.push_back(9);    
    sprintf(filename, "/nvdata/tftpboot/cap%d.png",cap);
    string file = string(filename);
    imwrite(file, image, compression_params);
}

gboolean timefinished(gpointer data)
{
    Q_UNUSED(data);
    //exit loop
    g_main_loop_quit(loop);
    return false;
}







