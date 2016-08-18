#ifndef STREAM_H
#define STREAM_H

#include <QObject>
#include <QDebug>
#include <QTime>
#include <QTimer>
#include <vector>
#include <string>

#include "gst/gst.h"
#include "gst/app/gstappsink.h"
#include "glib-2.0/glib.h"
#include "opencv2/opencv.hpp"
#include "opencv2/highgui/highgui.hpp"

using namespace std;


class stream : public QObject
{
    Q_OBJECT
public:
    stream();
    bool buildpipeline();
    bool trainrecogniser(string name);
    void startstream();
};


class Task: public QObject{
    Q_OBJECT
public:
    Task(QObject* parent=0) : QObject(parent){}
private:

public slots:
    void run();
signals:
    void finished();
};

#endif // STREAM_H
