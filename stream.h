#ifndef STREAM_H
#define STREAM_H

#include <QObject>
#include <QTimer>
#include <QDebug>

#include "gst/gst.h"
#include "gst/app/gstappsink.h"
#include "glib-2.0/glib.h"
#include "opencv2/opencv.hpp"



class stream : public QObject
{
    Q_OBJECT
public:
    stream();
    void buildpipeline();

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
