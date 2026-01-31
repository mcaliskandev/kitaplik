#include "kitaplik.hpp"

#include <QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    Kitaplik fm;
    fm.resize(900, 600);
    fm.show();

    return app.exec();
}
