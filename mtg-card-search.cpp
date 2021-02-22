#include <ostream>
#include <exception>

#include "obs-module.h"
#include <obs.hpp>
#include <util/config-file.h>
#include <filesystem>

#include "cpr/cpr.h"
#include "json.hpp"

#include <QMainWindow>
#include <QScrollArea>
#include <QGridLayout>
#include <QPushButton>
#include <QJsonObject>
#include <QLabel>
#include <QDockWidget>
#include <QEvent>
#include <QKeyEvent>
#include <QThread>
#include <QComboBox>
#include <QPushButton>
#include <QAction>
#include <QObject>
#include <QLineEdit>

#include <obs-frontend-api.h>
#include "../../UI/qt-wrappers.cpp"

using json = nlohmann::json;

#define mtgCardSearchConfigurationSection "obs-mtg-card-search"
#define PngExtension ".png"
// There is an issue somewhere relating to cpr library sending an HTTPS request. If the SCRYFALL_URL uses HTTPS, a 505 status is received.
// However, when sending an http request, the API redirects the connection to HTTPS and everything works correctly.
// TODO: Investigate issue with HTTPS connection.
const std::string SCRYFALL_URL = "http://api.scryfall.com/cards/search";
const std::string APPDATA_FOLDER_NAME = "\\OBSMTGCardSearch\\";

struct cardImage {
	const char * imageData;
	int dataSize;
};

struct cardData {
	std::string name;
	std::string cardImageUri;
};

class imageRetrievalException : public std::exception {
	virtual const char *what() const throw()
	{
		return "Failed to retrieve image.";
	}
} imageRetrievalEx;

class searchException : public std::exception {
	virtual const char *what() const throw()
	{
		return "Failed to complete search.";
	}
} searchEx;

class MtgCardSearchWidget : public QDockWidget {
public:
	MtgCardSearchWidget(QWidget *parent = 0)
		: QDockWidget(parent), reopenShown_(false)
	{

		main = (QMainWindow *)obs_frontend_get_main_window();

		const char* appdataPath = (getenv("appdata") + APPDATA_FOLDER_NAME).c_str();
		std::filesystem::create_directory(appdataPath);
		appdataPath_ = (std::string)appdataPath;
		setWindowTitle("MTG Card Search");
		setFeatures((DockWidgetFeatures)(AllDockWidgetFeatures &
						 ~DockWidgetClosable));
		QObject::connect(this, &QDockWidget::dockLocationChanged,
				 [this](Qt::DockWidgetArea area) {
					 dockLocation_ = area;
				 });

		sourceCombo_ = new QComboBox();
		
		scroll_ = new QScrollArea(this);
		scroll_->move(0, 22);

		container_ = new QWidget(this);
		layout_ = new QGridLayout(container_);
		layout_->setAlignment(Qt::AlignmentFlag::AlignTop);
		cardName_ = new QLineEdit();

		QGridLayout* sourcesWrapper = new QGridLayout(container_);
		QLabel *sourceLabel = new QLabel("Sources:", this);

		sourcesWrapper->addWidget(sourceLabel, 0, 0);
		sourcesWrapper->addWidget(sourceCombo_, 0, 1);

		auto updateButton_ = new QPushButton("Update", container_);
		QObject::connect(updateButton_, &QPushButton::clicked,
				 [this]() { onUpdateButtonClick(); });

		layout_->addLayout(sourcesWrapper, 1, 0);
		layout_->addWidget(cardName_);
		layout_->addWidget(updateButton_);

		scroll_->setWidgetResizable(true);
		scroll_->setWidget(container_);

		setLayout(layout_);
		resize(200, 400);
	}


	void saveConfig() {
		config_set_int(obs_frontend_get_global_config(),
			       mtgCardSearchConfigurationSection,
			       "dockLocation", (int)dockLocation_);
		config_set_bool(obs_frontend_get_global_config(),
				mtgCardSearchConfigurationSection,
				"dockVisible", dockVisible_);
	}

	void loadConfig() {

	}

private:
	QWidget* container_ = 0;
	QScrollArea* scroll_ = 0;
	QGridLayout* layout_ = 0;
	QPushButton* addButton_ = 0;
	int dockLocation_ = 0;
	bool dockVisible_ = false;
	bool reopenShown_ = false;
	QComboBox* sourceCombo_;
	QLineEdit* cardName_;
	std::string appdataPath_;
	QMainWindow* main;

	static bool add_sources(void *data, obs_source_t *source)
	{
		QComboBox *names = static_cast<QComboBox *>(data);
		uint32_t caps = obs_source_get_output_flags(source);
		QString name = obs_source_get_name(source);
		OBSWeakSource weak = OBSGetWeakRef(source);
		names->addItem(name);
		return true;
	}

	void refreshList(QComboBox *list)
	{
		obs_enum_sources(add_sources, list);
	}

	bool event(QEvent *event) override
	{
		if (event->type() == QEvent::Resize) {
			scroll_->resize(width(), height() - 22);
		}
		if (event->type() == QEvent::KeyRelease) {
			QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
			if ((keyEvent->key() == Qt::Key_Enter) ||
			    (keyEvent->key() == Qt::Key_Return)) {
				onUpdateButtonClick();
			}
		}
		if (event->type() == QEvent::Show) {
			refreshList(sourceCombo_);
		}
		return QDockWidget::event(event);
	}

	void onUpdateButtonClick()
{
		std::string query =
			cardName_->text().toLocal8Bit().data();
		try {
			std::vector<cardData> cards = search(query);
			if (cards.size() > 0) {
				cardData targetCard = cards[0];
				cardImage newImage =
					getCardImage(targetCard.cardImageUri);
				const char *filename =
					(appdataPath_ + targetCard.name +
						PngExtension)
						.c_str();
				saveCardImage(filename, newImage.imageData,
						newImage.dataSize);
				updateCardImageSource(filename);
			}
		} catch (imageRetrievalException &ex) {
			OBSMessageBox::warning(main, "Error", "Unable to succesfully locate image.");
		} catch (searchException &ex) {
			OBSMessageBox::warning(main, "Error", "Search failed.");
		} catch (std::exception& ex) {
			OBSMessageBox::warning(main, "Error", ex.what());
		}
	}

	std::vector<cardData> search(std::string query)
	{
		std::vector<cardData> searchResult;
		std::string requestUrl = SCRYFALL_URL + "?q=" + query;
		try {
			cpr::AsyncResponse responsePromise =
				cpr::GetAsync((cpr::Url{requestUrl}));
			cpr::Response searchResponse = responsePromise.get();
			//TODO: Add Try-catch and error handling for search
			json result;
			result = json::parse(searchResponse.text);

			auto cardResults = result["data"];
			for (json::iterator it = cardResults.begin();
			     it != cardResults.end(); ++it) {
				searchResult.push_back(getCardData(it.value()));
			}
		} catch (std::exception &e) {
			blog(LOG_ERROR, "Search failed.");
		}

		if (searchResult.empty()) {
			OBSMessageBox::information(main, "No results", "Search did not find any results.");
		}
		return searchResult;
	}

	cardData getCardData(json card)
	{
		cardData result;

		result.name = card["name"];
		result.cardImageUri = card["image_uris"]["large"];

		return result;
	}

	cardImage getCardImage(std::string cardUri)
	{
		cardImage result;
		try {
			cpr::AsyncResponse requestPromise =
				cpr::GetAsync((cpr::Url{cardUri}));
			cpr::Response cardResponse = requestPromise.get();

			//TODO: Handle multiple results instead of just getting the first
			const char *resultText = &cardResponse.text[0];
			result.dataSize = cardResponse.downloaded_bytes;
			result.imageData = resultText;
		} catch (int e) {
			blog(LOG_ERROR,
			     "Failed to retrieve image.");
			throw imageRetrievalEx;
		}

		return result;

	}

	void saveCardImage(const char *filename, const char *imageData,
			   int dataSize)
	{
		std::ofstream myStream(filename, std::ios::binary);
		
		if (!myStream.is_open()) {
			blog(LOG_ERROR,
			     "Failed to open stream for saving image.");
			return;
		}

		myStream.write(imageData, dataSize);
		myStream.close();
	}

	void updateCardImageSource(const char *imageFile)
	{
		const char *targetSourceName =
			sourceCombo_->currentText().toLocal8Bit().data();
		obs_source_t *target = obs_get_source_by_name(targetSourceName);
		obs_data_t *settings = obs_source_get_settings(target);
		obs_data_set_string(settings, "file", imageFile);

		obs_source_update(target, settings);
		obs_source_release(target);
		obs_data_release(settings);
	}
};

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Chris Nearman (@sheepparade)")

QThread *g_uiThread;

template<class T> bool RunInUIThread(T &&func)
{
	if (g_uiThread == nullptr)
		return false;
	QMetaObject::invokeMethod(g_uiThread,
				  [func = std::move(func)]() { func(); });
	return true;
}

bool obs_module_load()
{
	if (obs_get_version() < MAKE_SEMANTIC_VERSION(25, 0, 0))
		return false;

	auto mainwin = (QMainWindow *)obs_frontend_get_main_window();
	if (mainwin == nullptr)
		return false;
	QMetaObject::invokeMethod(mainwin, []() {
		g_uiThread = QThread::currentThread();
	});

	auto dock = new MtgCardSearchWidget(mainwin);
	auto action = (QAction *)obs_frontend_add_dock(dock);

	// begin work around obs not remember dock geometry added by obs_frontend_add_dock
	auto docklocation = config_get_int(obs_frontend_get_global_config(), mtgCardSearchConfigurationSection, "dockLocation");
	auto visible = config_get_bool(obs_frontend_get_global_config(), mtgCardSearchConfigurationSection, "dockVisible");
	if (!config_has_user_value(obs_frontend_get_global_config(), mtgCardSearchConfigurationSection, "dockLocation")) {
		docklocation = Qt::DockWidgetArea::LeftDockWidgetArea;
	}
	if (!config_has_user_value(obs_frontend_get_global_config(), mtgCardSearchConfigurationSection, "dockVisible")) {
		visible = true;
	}

	mainwin->addDockWidget((Qt::DockWidgetArea)docklocation, dock);
	if (visible) {
		dock->setVisible(true);
		action->setChecked(true);
	} else {
		dock->setVisible(false);
		action->setChecked(false);
	}
	// end work around

	obs_frontend_add_event_callback(
		[](enum obs_frontend_event event, void *private_data) {
			if (event ==
			    obs_frontend_event::OBS_FRONTEND_EVENT_EXIT) {
				static_cast<MtgCardSearchWidget *>(private_data)
					->saveConfig();
			}
		},
		dock);

	return true;
}
