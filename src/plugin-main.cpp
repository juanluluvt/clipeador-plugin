/*
Clipeador - panel de OBS para crear clips y marcadores de Twitch
Copyright (C) 2026 juanluluvt

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/bmem.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QDir>
#include <QDateTime>
#include <QPair>
#include <QList>
#include <QFile>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

#include <functional>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

const char *DOCK_ID = "clipeador-dock";
const char *SCOPES = "clips:edit channel:manage_broadcast";
const QLatin1String API("https://api.twitch.tv/helix");

using Reply = std::function<void(int, const QJsonObject &)>;

class Clipeador : public QWidget {
public:
	explicit Clipeador(QWidget *parent) : QWidget(parent)
	{
		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(8, 8, 8, 8);
		root->setSpacing(8);

		auto *grid = new QGridLayout();
		grid->setSpacing(8);

		b30 = makeButton("30 s\nClip Twitch", "#9146ff", "#ffffff");
		b60 = makeButton("60 s\nClip Twitch", "#9146ff", "#ffffff");
		b90 = makeButton("90 s\nLocal (OBS)", "#f59e0b", "#241500");
		bMark = makeButton("Marcador en el VOD", "#00c2a8", "#04201c");
		grid->addWidget(b30, 0, 0);
		grid->addWidget(b60, 0, 1);
		grid->addWidget(b90, 0, 2);
		grid->addWidget(bMark, 1, 0, 1, 3);
		root->addLayout(grid);

		status = new QLabel("Pulsa Conectar con Twitch para empezar.");
		status->setWordWrap(true);
		status->setTextFormat(Qt::RichText);
		status->setOpenExternalLinks(true);
		status->setTextInteractionFlags(Qt::TextBrowserInteraction);
		status->setMinimumHeight(48);
		status->setStyleSheet("QLabel{padding:6px;border:1px solid #444;border-radius:6px;}");
		root->addWidget(status);

		clientLabel = new QLabel("Client ID de tu app de Twitch (30 caracteres):");
		clientEdit = new QLineEdit();
		clientEdit->setPlaceholderText("abcd1234efgh5678ijkl9012mnop34");
		root->addWidget(clientLabel);
		root->addWidget(clientEdit);

		descEdit = new QLineEdit();
		descEdit->setPlaceholderText("Descripción del marcador (opcional)");
		descEdit->setMaxLength(140);
		root->addWidget(descEdit);

		bConnect = new QPushButton("Conectar con Twitch");
		root->addWidget(bConnect);
		root->addStretch(1);

		connect(b30, &QPushButton::clicked, this, [this]() { makeClip(30); });
		connect(b60, &QPushButton::clicked, this, [this]() { makeClip(60); });
		connect(b90, &QPushButton::clicked, this, [this]() { saveReplay(); });
		connect(bMark, &QPushButton::clicked, this, [this]() { makeMarker(); });
		connect(bConnect, &QPushButton::clicked, this, [this]() {
			if (!token.isEmpty())
				disconnectAccount();
			else
				startDeviceFlow();
		});
		connect(&pollTimer, &QTimer::timeout, this, [this]() { pollDevice(); });
		connect(descEdit, &QLineEdit::editingFinished, this, [this]() { save(); });

		load();
		clientEdit->setText(clientId);
		descEdit->setText(markerDesc);
		refreshUi();
		if (!token.isEmpty())
			setStatus(login.isEmpty() ? "Listo." : QString("Listo, %1.").arg(login), "ok");
	}

private:
	QNetworkAccessManager net;
	QPushButton *b30, *b60, *b90, *bMark, *bConnect;
	QLabel *status, *clientLabel;
	QLineEdit *clientEdit, *descEdit;
	QTimer pollTimer;

	QString clientId, token, refreshTok, userId, login, markerDesc;
	QString deviceCode;
	int pollMs = 5000;
	qint64 deviceDeadline = 0;

	/* ---------- UI ---------- */

	QPushButton *makeButton(const QString &text, const QString &bg, const QString &fg)
	{
		auto *b = new QPushButton(text);
		b->setMinimumHeight(56);
		b->setCursor(Qt::PointingHandCursor);
		b->setStyleSheet(QString("QPushButton{background:%1;color:%2;border:0;border-radius:10px;"
					 "font-weight:bold;font-size:13px;padding:8px;}"
					 "QPushButton:disabled{background:#555;color:#999;}")
					 .arg(bg, fg));
		return b;
	}

	void setStatus(const QString &msg, const QString &kind = QString())
	{
		QString color = kind == "ok" ? "#4ade80" : kind == "err" ? "#ff6b6b" : "#bbbbbb";
		status->setText(QString("<span style='color:%1'>%2</span>").arg(color, msg));
	}

	void setBusy(bool busy)
	{
		b30->setEnabled(!busy);
		b60->setEnabled(!busy);
		b90->setEnabled(!busy);
		bMark->setEnabled(!busy);
	}

	void refreshUi()
	{
		bool connected = !token.isEmpty();
		bConnect->setText(connected ? "Desconectar" : "Conectar con Twitch");
		clientLabel->setVisible(!connected);
		clientEdit->setVisible(!connected);
	}

	/* ---------- Configuración ---------- */

	QString cfgPath()
	{
		char *p = obs_module_config_path("");
		QString dir = p ? QString::fromUtf8(p) : QString();
		bfree(p);
		QDir().mkpath(dir);
		return QDir(dir).filePath("clipeador.json");
	}

	void load()
	{
		QFile f(cfgPath());
		if (!f.open(QIODevice::ReadOnly))
			return;
		QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
		clientId = o["clientId"].toString();
		token = o["token"].toString();
		refreshTok = o["refresh"].toString();
		userId = o["userId"].toString();
		login = o["login"].toString();
		markerDesc = o["markerDesc"].toString();
	}

	void save()
	{
		markerDesc = descEdit->text().trimmed();
		QJsonObject o;
		o["clientId"] = clientId;
		o["token"] = token;
		o["refresh"] = refreshTok;
		o["userId"] = userId;
		o["login"] = login;
		o["markerDesc"] = markerDesc;
		QFile f(cfgPath());
		if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
			f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
	}

	/* ---------- Red ---------- */

	void request(bool post, const QUrl &url, const QByteArray &body, const QByteArray &ctype, bool auth,
		     Reply cb, bool canRefresh = true)
	{
		QNetworkRequest rq(url);
		rq.setTransferTimeout(15000);
		if (!ctype.isEmpty())
			rq.setHeader(QNetworkRequest::ContentTypeHeader, ctype);
		if (auth) {
			rq.setRawHeader("Client-Id", clientId.toUtf8());
			rq.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
		}
		QNetworkReply *r = post ? net.post(rq, body) : net.get(rq);
		connect(r, &QNetworkReply::finished, this,
			[this, r, post, url, body, ctype, auth, cb, canRefresh]() {
				int st = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				QJsonObject obj = QJsonDocument::fromJson(r->readAll()).object();
				if (st == 0)
					obj["message"] = r->errorString();
				r->deleteLater();

				if (st == 401 && auth && canRefresh && !refreshTok.isEmpty()) {
					refreshToken([this, post, url, body, ctype, auth, cb, st, obj](bool ok) {
						if (ok)
							request(post, url, body, ctype, auth, cb, false);
						else
							cb(st, obj);
					});
					return;
				}
				cb(st, obj);
			});
	}

	static QByteArray form(const QList<QPair<QString, QString>> &items)
	{
		QUrlQuery q;
		for (const auto &i : items)
			q.addQueryItem(i.first, i.second);
		return q.toString(QUrl::FullyEncoded).toUtf8();
	}

	void refreshToken(std::function<void(bool)> done)
	{
		QByteArray body = form({{"client_id", clientId},
					{"grant_type", "refresh_token"},
					{"refresh_token", refreshTok}});
		request(true, QUrl("https://id.twitch.tv/oauth2/token"), body,
			"application/x-www-form-urlencoded", false,
			[this, done](int st, const QJsonObject &o) {
				if (st == 200 && !o["access_token"].toString().isEmpty()) {
					token = o["access_token"].toString();
					if (!o["refresh_token"].toString().isEmpty())
						refreshTok = o["refresh_token"].toString();
					save();
					done(true);
				} else {
					token.clear();
					refreshTok.clear();
					save();
					refreshUi();
					done(false);
				}
			});
	}

	QString explain(int st, const QJsonObject &o)
	{
		if (st == 401)
			return "Sesión caducada. Pulsa Conectar con Twitch otra vez.";
		if (st == 403)
			return "Faltan permisos. Desconecta y vuelve a conectar para autorizarlos.";
		if (st == 404)
			return "No estás en directo o no hay VOD activo.";
		if (st == 0)
			return "Sin conexión con Twitch: " + o["message"].toString();
		return QString("Error %1: %2").arg(st).arg(o["message"].toString());
	}

	/* ---------- Conexión con Twitch (código de dispositivo) ---------- */

	void startDeviceFlow()
	{
		clientId = clientEdit->text().trimmed();
		if (!QRegularExpression("^[A-Za-z0-9]{30}$").match(clientId).hasMatch()) {
			setStatus("Eso no es un Client ID. Es un código de 30 letras y números que sale en "
				  "dev.twitch.tv/console/apps, dentro de tu aplicación. No es el nombre de tu canal.",
				  "err");
			return;
		}
		save();
		bConnect->setEnabled(false);
		setStatus("Pidiendo código a Twitch…");

		QByteArray body = form({{"client_id", clientId}, {"scopes", SCOPES}});
		request(true, QUrl("https://id.twitch.tv/oauth2/device"), body,
			"application/x-www-form-urlencoded", false, [this](int st, const QJsonObject &o) {
				if (st != 200 || o["device_code"].toString().isEmpty()) {
					bConnect->setEnabled(true);
					setStatus("No se pudo iniciar la conexión: " + o["message"].toString() +
							  ". Comprueba el Client ID y que la app sea de tipo Public.",
						  "err");
					return;
				}
				deviceCode = o["device_code"].toString();
				pollMs = qMax(1, o["interval"].toInt(5)) * 1000;
				deviceDeadline = QDateTime::currentSecsSinceEpoch() + o["expires_in"].toInt(1800);
				QString uri = o["verification_uri"].toString();
				QString code = o["user_code"].toString();
				QDesktopServices::openUrl(QUrl(uri));
				setStatus(QString("Se ha abierto Twitch en tu navegador. Pulsa <b>Autorizar</b>. "
						  "Si te pide un código es <b>%1</b>. Si no se abre, entra en "
						  "<a href='%2'>este enlace</a>.")
						  .arg(code, uri));
				pollTimer.start(pollMs);
			});
	}

	void pollDevice()
	{
		if (QDateTime::currentSecsSinceEpoch() > deviceDeadline) {
			pollTimer.stop();
			bConnect->setEnabled(true);
			setStatus("El código ha caducado. Pulsa Conectar con Twitch otra vez.", "err");
			return;
		}
		pollTimer.stop(); /* se reactiva al recibir respuesta, para no solapar peticiones */
		QByteArray body = form({{"client_id", clientId},
					{"scopes", SCOPES},
					{"device_code", deviceCode},
					{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}});
		request(true, QUrl("https://id.twitch.tv/oauth2/token"), body,
			"application/x-www-form-urlencoded", false, [this](int st, const QJsonObject &o) {
				if (st == 200 && !o["access_token"].toString().isEmpty()) {
					token = o["access_token"].toString();
					refreshTok = o["refresh_token"].toString();
					userId.clear();
					save();
					ensureUser([this](bool ok, const QJsonObject &e) {
						bConnect->setEnabled(true);
						refreshUi();
						if (ok)
							setStatus(QString("Conectado como %1.").arg(login), "ok");
						else
							setStatus(e["message"].toString(), "err");
					});
					return;
				}
				QString msg = o["message"].toString();
				if (msg == "authorization_pending") {
					pollTimer.start(pollMs);
				} else if (msg == "slow_down") {
					pollMs += 5000;
					pollTimer.start(pollMs);
				} else {
					bConnect->setEnabled(true);
					setStatus("No se pudo completar la conexión: " + msg, "err");
				}
			});
	}

	void disconnectAccount()
	{
		token.clear();
		refreshTok.clear();
		userId.clear();
		login.clear();
		save();
		refreshUi();
		setStatus("Desconectado.");
	}

	void ensureUser(std::function<void(bool, const QJsonObject &)> cb)
	{
		if (!userId.isEmpty()) {
			cb(true, QJsonObject());
			return;
		}
		request(false, QUrl(QString("%1/users").arg(API)), QByteArray(), QByteArray(), true,
			[this, cb](int st, const QJsonObject &o) {
				QJsonArray data = o["data"].toArray();
				if (st == 200 && !data.isEmpty()) {
					QJsonObject u = data[0].toObject();
					userId = u["id"].toString();
					login = u["display_name"].toString();
					save();
					cb(true, o);
				} else {
					QJsonObject e = o;
					e["message"] = explain(st, o);
					cb(false, e);
				}
			});
	}

	bool needAuth()
	{
		if (token.isEmpty()) {
			setStatus("Conecta tu cuenta de Twitch primero.", "err");
			return true;
		}
		return false;
	}

	/* ---------- Acciones ---------- */

	void makeClip(int seconds)
	{
		if (needAuth())
			return;
		setBusy(true);
		setStatus(QString("Creando clip de %1 s…").arg(seconds));
		ensureUser([this, seconds](bool ok, const QJsonObject &e) {
			if (!ok) {
				setBusy(false);
				setStatus(e["message"].toString(), "err");
				return;
			}
			QUrl url(QString("%1/clips?broadcaster_id=%2&duration=%3").arg(API, userId).arg(seconds));
			request(true, url, QByteArray(), QByteArray(), true,
				[this, seconds](int st, const QJsonObject &o) {
					QJsonArray data = o["data"].toArray();
					if ((st == 200 || st == 202) && !data.isEmpty()) {
						QString id = data[0].toObject()["id"].toString();
						setStatus("Clip creado. Comprobando que se ha publicado…");
						waitForClip(id, seconds, 0);
					} else {
						setBusy(false);
						setStatus(explain(st, o), "err");
					}
				});
		});
	}

	void waitForClip(const QString &id, int seconds, int attempt)
	{
		if (attempt >= 8) {
			setBusy(false);
			setStatus("Clip enviado, pero aún no aparece. Revisa tu gestor de clips en unos segundos.",
				  "ok");
			return;
		}
		QTimer::singleShot(2500, this, [this, id, seconds, attempt]() {
			QUrl url(QString("%1/clips?id=%2").arg(API, id));
			request(false, url, QByteArray(), QByteArray(), true,
				[this, id, seconds, attempt](int st, const QJsonObject &o) {
					QJsonArray data = o["data"].toArray();
					if (st == 200 && !data.isEmpty()) {
						QString link = data[0].toObject()["url"].toString();
						setBusy(false);
						setStatus(QString("Clip de %1 s guardado en Twitch: <a href='%2'>%2</a>")
								  .arg(seconds)
								  .arg(link),
							  "ok");
					} else {
						waitForClip(id, seconds, attempt + 1);
					}
				});
		});
	}

	void makeMarker()
	{
		if (needAuth())
			return;
		setBusy(true);
		setStatus("Creando marcador…");
		ensureUser([this](bool ok, const QJsonObject &e) {
			if (!ok) {
				setBusy(false);
				setStatus(e["message"].toString(), "err");
				return;
			}
			QJsonObject body;
			body["user_id"] = userId;
			QString d = descEdit->text().trimmed().left(140);
			if (!d.isEmpty())
				body["description"] = d;
			save();
			request(true, QUrl(QString("%1/streams/markers").arg(API)),
				QJsonDocument(body).toJson(QJsonDocument::Compact), "application/json", true,
				[this](int st, const QJsonObject &o) {
					setBusy(false);
					QJsonArray data = o["data"].toArray();
					if (st == 200 && !data.isEmpty()) {
						int pos = data[0].toObject()["position_seconds"].toInt();
						setStatus(QString("Marcador creado en el minuto %1:%2 del VOD.")
								  .arg(pos / 60)
								  .arg(pos % 60, 2, 10, QChar('0')),
							  "ok");
					} else if (st == 404) {
						setStatus("No se pudo crear: tienes que estar en directo y con VODs activados.",
							  "err");
					} else {
						setStatus(explain(st, o), "err");
					}
				});
		});
	}

	void saveReplay()
	{
		if (!obs_frontend_replay_buffer_active()) {
			setStatus("El búfer de repetición no está activo. Pon la duración en 90 s en Ajustes > Salida "
				  "> Búfer de repetición y pulsa Iniciar búfer de repetición.",
				  "err");
			return;
		}
		obs_frontend_replay_buffer_save();
		setStatus("Guardando los últimos 90 s…");
		QTimer::singleShot(1500, this, [this]() {
			char *p = obs_frontend_get_last_replay();
			QString path = p ? QString::fromUtf8(p) : QString();
			bfree(p);
			setStatus(QString("Replay guardado en tu disco%1. El búfer debe estar configurado en 90 s.")
					  .arg(path.isEmpty() ? QString() : ": " + path),
				  "ok");
		});
	}
};

} // namespace

bool obs_module_load(void)
{
	auto *dock = new Clipeador(nullptr);
	if (!obs_frontend_add_dock_by_id(DOCK_ID, "Clipeador", dock)) {
		obs_log(LOG_ERROR, "no se pudo registrar el panel Clipeador");
		delete dock;
		return false;
	}
	obs_log(LOG_INFO, "Clipeador cargado (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_dock(DOCK_ID);
	obs_log(LOG_INFO, "Clipeador descargado");
}
