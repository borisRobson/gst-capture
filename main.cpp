#include <QCoreApplication>
#include "stream.h"

stream *strm;
char* name;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if(argc >= 1)
        name = argv[1];

    //parent task to app for cleanup
    Task* task = new Task(&app);

    QObject::connect(task, SIGNAL(finished()), &app, SLOT(quit()));

    //init gstreamer
    gst_init(&argc,&argv);

    //run program
    QTimer::singleShot(10,task,SLOT(run()));

    return app.exec();
}

void Task::run()
{
    bool pipeBuilt;
    bool recogniserBuilt;
    qDebug() << __FUNCTION__;

    //build and run the pipeline
    strm = new stream();

    //create and link pipeline elements
    pipeBuilt = strm->buildpipeline();
    //load database image and train faceRecognizer model
    std::string user(name);
    recogniserBuilt = strm->trainrecogniser(user);

    //if both successful; run main loop
    if(pipeBuilt && recogniserBuilt)
        strm->startstream();

    //quit when done
    QCoreApplication::instance()->quit();

    return;
}
