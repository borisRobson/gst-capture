#include <QCoreApplication>
#include "stream.h"

stream *strm;
char* name;
string user;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);


    //if(argc >= 1)
    //    name = argv[1];

    //parent task to app for cleanup
    Task* task = new Task(&app);

    QObject::connect(task, SIGNAL(finished()), &app, SLOT(quit()));

    //init gstreamer
    gst_init(&argc,&argv);

    //start with command line arg
    if(argc > 1){
        name = argv[1];
        std::string usr(name);
        user = usr;
    }/*
    else{   //or read in file
        qDebug() << "Facial Recognition";
        qDebug() << "Enter user number & press doorbell to begin";
        QString line;
        QFile inputfile("/tmp/keypressed.txt");
        while(true){
            if(inputfile.exists()){
                if(inputfile.open(QIODevice::ReadOnly)){
                    QTextStream in(&inputfile);
                    line = in.readLine();
                    inputfile.close();
                    inputfile.remove();
                    break;
                }
            }
        }
        if(line == QString("0")){
            user = "Brandon";
        }
        else if (line == QString("1")){
            user = "Max";
        }
        else if(line == QString("2")){
            user = "Jack";
        }
    }*/

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

    qDebug() << "Facial Recognition";
    qDebug() << "Enter user number & press doorbell to begin";
    QString line;
    QFile inputfile("/tmp/keypressed.txt");
    while(true){
        if(inputfile.exists()){
            if(inputfile.open(QIODevice::ReadOnly)){
                QTextStream in(&inputfile);
                line = in.readLine();
                inputfile.close();
                inputfile.remove();
                break;
            }
        }
    }
    if(line == QString("0")){
        user = "Brandon";
    }
    else if (line == QString("1")){
        user = "Max";
    }
    else if(line == QString("2")){
        user = "Jack";
    }

    //load database image and train faceRecognizer model    
    recogniserBuilt = strm->trainrecogniser(user);

    //if both successful; run main loop
    if(pipeBuilt && recogniserBuilt)
        strm->startstream();

    //quit when done
    QCoreApplication::instance()->quit();

    return;
}
