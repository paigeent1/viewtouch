#include <catch2/catch_test_macros.hpp>

#include "app/pos_service.hh"
#include "layout_fixture.hh"
#include "layoutcontroller.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// The store's pictures: added by a manager, kept in the database, shown by name.

namespace {
QByteArray pngBytes(int w = 40, int h = 20, QColor color = Qt::red)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
QString b64(const QByteArray &d) { return QString::fromLatin1(d.toBase64()); }
} // namespace

TEST_CASE("Pictures: added by a manager under a plain name, shown by ref, removed", "[images]")
{
    QTemporaryDir cache;
    PosService pos(test::seedPosData(), nullptr);
    pos.shared()->imageCacheDir = cache.path();
    REQUIRE(pos.loginWithPin(u"1111"_s));                            // a server can't
    CHECK_FALSE(pos.addStoreImage(u"logo.png"_s, b64(pngBytes())));
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));

    CHECK(app::PosSession::storeImageRef(u"Our Logo FINAL.PNG"_s) == u"store:our-logo-final.png"_s);
    CHECK(app::PosSession::storeImageRef(u"menu.pdf"_s).isEmpty());
    CHECK_FALSE(pos.addStoreImage(u"notes.txt"_s, b64("hello")));
    CHECK_FALSE(pos.addStoreImage(u"fake.png"_s, b64("not a picture")));
    REQUIRE(pos.addStoreImage(u"Our Logo FINAL.PNG"_s, b64(pngBytes())));
    REQUIRE(pos.storeImages().size() == 1);
    CHECK(pos.storeImages()[0].toMap()[u"ref"_s] == u"store:our-logo-final.png"_s);

    // As a file this computer's screens can load; by content, so a new one is a new file.
    const QString url = pos.imageUrl(u"store:our-logo-final.png"_s);
    REQUIRE(url.startsWith(u"file:"_s));
    CHECK(QImage(QUrl(url).toLocalFile()).width() == 40);
    REQUIRE(pos.addStoreImage(u"our logo final.png"_s, b64(pngBytes(60, 30))));   // replaced
    CHECK(pos.storeImages().size() == 1);
    CHECK(QImage(QUrl(pos.imageUrl(u"store:our-logo-final.png"_s)).toLocalFile()).width() == 60);

    // The store logo, wherever it's used.
    pos.shared()->settings.displayLogo = "store:our-logo-final.png";
    CHECK(pos.imageUrl(u"logo:"_s) == pos.imageUrl(u"store:our-logo-final.png"_s));
    CHECK(pos.imageUrl(u"qrc:/images/burger.png"_s) == u"qrc:/images/burger.png"_s);
    CHECK(pos.imageUrl(u"store:missing.png"_s).isEmpty());

    // Paired screens fetch it by ref; only the store's pictures, never other files.
    CHECK(QByteArray::fromBase64(pos.storeImage(u"store:our-logo-final.png"_s).toLatin1()) == pngBytes(60, 30));
    CHECK(pos.storeImage(u"store:../../etc/passwd"_s).isEmpty());

    REQUIRE(pos.removeStoreImage(u"store:our-logo-final.png"_s));
    CHECK(pos.storeImages().isEmpty());
}

TEST_CASE("Pictures: kept in the store's database", "[images][posstore]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    const auto seed = test::seedPosData();
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        REQUIRE(store.seed(seed.settings, seed.menu, seed.employees));
    }
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        storage::AsyncWriter writer(path);
        storage::SqlPosSink sink(writer);
        PosService pos(*store.load(), &sink);
        REQUIRE(pos.loginWithPin(u"1234"_s));
        REQUIRE(pos.addStoreImage(u"logo.png"_s, b64(pngBytes())));
        REQUIRE(pos.addStoreImage(u"patio.png"_s, b64(pngBytes(10, 10, Qt::green))));
        REQUIRE(pos.removeStoreImage(u"patio.png"_s));
    }
    storage::PosStore store(path);
    REQUIRE(store.open());
    const auto data = store.load();
    REQUIRE(data);
    REQUIRE(data->images.size() == 1);
    CHECK(data->images.at("logo.png") == pngBytes());
}

TEST_CASE("Pictures: the touch picker finds them in Pictures and Documents, newest first", "[images][picker]")
{
    QTemporaryDir home;
    REQUIRE(home.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.path().toUtf8());
    QDir(home.path()).mkpath(u"Pictures/Logos"_s);
    QDir(home.path()).mkpath(u"Documents"_s);
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(Qt::red);
    REQUIRE(img.save(home.filePath(u"Documents/older.png"_s)));
    QFile(home.filePath(u"Documents/older.png"_s)).setFileTime(QDateTime::currentDateTime().addDays(-1), QFileDevice::FileModificationTime);
    REQUIRE(img.save(home.filePath(u"Pictures/Logos/logo.png"_s)));
    QFile notes(home.filePath(u"Documents/notes.txt"_s));
    REQUIRE(notes.open(QIODevice::WriteOnly));
    notes.write("not a picture");
    notes.close();

    PosService pos(test::seedPosData(), nullptr);
    QStringList names;
    for (const QVariant &v : pos.pictureFiles())
        if (v.toMap()[u"path"_s].toString().startsWith(home.path()))
            names << v.toMap()[u"name"_s].toString();
    qputenv("HOME", oldHome);
    CHECK(names == QStringList{u"logo.png"_s, u"older.png"_s});   // one folder down too; no text files
}

TEST_CASE("Pictures: one without a name ending (Android's photo picker) is named by what it is", "[images][picker]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(Qt::blue);
    REQUIRE(img.save(dir.filePath(u"Logo"_s), "PNG"));     // no ".png"
    REQUIRE(img.save(dir.filePath(u"menu.dat"_s), "JPG"));  // the wrong one
    QFile text(dir.filePath(u"notes"_s));
    REQUIRE(text.open(QIODevice::WriteOnly));
    text.write("hello");
    text.close();

    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    CHECK(pos.addImageFile(dir.filePath(u"Logo"_s)) == u"store:logo.png"_s);
    CHECK(pos.addImageFile(QUrl::fromLocalFile(dir.filePath(u"menu.dat"_s)).toString()) == u"store:menu.jpg"_s);
    CHECK(pos.addImageFile(dir.filePath(u"notes"_s)).isEmpty());   // not a picture
}

TEST_CASE("Guest screen pictures: one from the Dropbox folder is made to fit, kept and shown in turn", "[images][guest]")
{
    QTemporaryDir home;
    REQUIRE(home.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.path().toUtf8());
    QDir(home.path()).mkpath(u".cache/vt-guest-pictures"_s);
    QDir(home.path()).mkpath(u"Elsewhere"_s);
    // A phone's big picture, copied from Dropbox; the same one somewhere else.
    QImage big(3000, 1000, QImage::Format_RGB32);
    big.fill(Qt::darkGreen);
    const QString cached = home.filePath(u".cache/vt-guest-pictures/Fall Sale!.png"_s);
    REQUIRE(big.save(cached));
    const QString elsewhere = home.filePath(u"Elsewhere/sale.png"_s);
    REQUIRE(big.save(elsewhere));
    QTemporaryDir images;
    auto l = vt::test::loadTestLayout();
    REQUIRE(l);
    PosService pos(test::seedPosData(), nullptr);
    pos.shared()->imageCacheDir = images.path();
    pos.shared()->settings.displaySlides = {"Welcome to the store"};
    LayoutController c(*l);
    c.setPos(&pos);

    REQUIRE(pos.loginWithPin(u"1111"_s));                      // a server can't
    CHECK(c.addGuestPicture(cached).isEmpty());
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));
    CHECK(c.addGuestPicture(elsewhere).isEmpty());             // only from the guest screen's folders
    CHECK(c.addGuestPicture(home.filePath(u".cache/vt-guest-pictures/../../Elsewhere/sale.png"_s)).isEmpty());
    const QString line = c.addGuestPicture(QUrl::fromLocalFile(cached).toString());
    qputenv("HOME", oldHome);

    CHECK(line == u"image:store:guest-fall-sale.jpg"_s);
    CHECK(pos.shared()->settings.displaySlides
          == std::vector<std::string>{"Welcome to the store", "image:store:guest-fall-sale.jpg"});
    CHECK(pos.shared()->images.size() == 1);
    // A JPEG no bigger than the guest screen (never made larger), kept by the store.
    const auto it = pos.shared()->images.find("guest-fall-sale.jpg");
    REQUIRE(it != pos.shared()->images.end());
    CHECK(it->second.startsWith("\xff\xd8"));
    const QImage kept = QImage::fromData(it->second);
    CHECK(kept.width() == 1920);
    CHECK(kept.height() == 640);
    // The guest screen shows it by its ref; Store Settings' change log has it.
    CHECK(pos.imageUrl(u"store:guest-fall-sale.jpg"_s).startsWith(u"file:"_s));
    CHECK(std::ranges::any_of(pos.changesToday(), [](const core::Change &ch) {
        return ch.after.find("guest-fall-sale.jpg") != std::string::npos;
    }));
}
