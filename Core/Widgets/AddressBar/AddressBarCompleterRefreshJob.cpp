﻿/***********************************************************************************
** MIT License                                                                    **
**                                                                                **
** Copyright (c) 2018 Victor DENIS (victordenis01@gmail.com)                      **
**                                                                                **
** Permission is hereby granted, free of charge, to any person obtaining a copy   **
** of this software and associated documentation files (the "Software"), to deal  **
** in the Software without restriction, including without limitation the rights   **
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell      **
** copies of the Software, and to permit persons to whom the Software is          **
** furnished to do so, subject to the following conditions:                       **
**                                                                                **
** The above copyright notice and this permission notice shall be included in all **
** copies or substantial portions of the Software.                                **
**                                                                                **
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR     **
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,       **
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE    **
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER         **
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,  **
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE  **
** SOFTWARE.                                                                      **
***********************************************************************************/

#include "AddressBarCompleterRefreshJob.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <QDateTime>

#include <algorithm>

#include <ndb/function.hpp>
#include <ndb/engine/sqlite/query.hpp>

#include "Widgets/AddressBar/AddressBarCompleterModel.hpp"

#include "Application.hpp"
#include <ndb/query.hpp>

constexpr auto& history = ndb::models::navigation.history;

namespace Sn
{
AddressBarCompleterRefreshJob::AddressBarCompleterRefreshJob(const QString& searchString):
	QObject(),
	m_searchString(searchString),
	m_timestamp(QDateTime::currentMSecsSinceEpoch())
{
	m_watcher = new QFutureWatcher<void>(this);
	connect(m_watcher, &QFutureWatcher<void>::finished, this, &AddressBarCompleterRefreshJob::slotFinished);

	QFuture<void> future = QtConcurrent::run(this, &AddressBarCompleterRefreshJob::runJob);
	m_watcher->setFuture(future);
}

void AddressBarCompleterRefreshJob::slotFinished()
{
	emit finished();
}

void AddressBarCompleterRefreshJob::jobCancelled()
{
	m_jobCancelled = true;
}

void AddressBarCompleterRefreshJob::runJob()
{
	if (m_jobCancelled || Application::instance()->isClosing() || !Application::instance())
		return;

	if (m_searchString.isEmpty())
		completeMostVisited();
	else
		completeFromHistory();

	foreach(QStandardItem* item, m_items) {
		if (m_jobCancelled)
			return;

		// TODO: icon
		item->setData(Application::getAppIcon("webpage"), AddressBarCompleterModel::ImageRole);
	}

	if (m_jobCancelled)
		return;

	if (!m_searchString.isEmpty()) {
		if (!(m_searchString.isEmpty() || m_searchString == QLatin1String("www."))) {
			ndb::sqlite_query<dbs::navigation> query = AddressBarCompleterModel::createDomainQuery(m_searchString);

			auto& result = query.exec<ndb::objects::history>();
			if (result.has_result())
				m_domainCompletion = createDomainCompletion(QUrl(QString::fromStdString(result[0].url)).host());
		}
	}

	if (m_jobCancelled)
		return;

	if (!m_searchString.isEmpty()) {
		QStandardItem* item{new QStandardItem()};
		item->setText(m_searchString);
		item->setData(m_searchString, AddressBarCompleterModel::UrlRole);
		item->setData(m_searchString, AddressBarCompleterModel::SearchStringRole);
		item->setData(QVariant(true), AddressBarCompleterModel::VisitSearchItemRole);

		if (!m_domainCompletion.isEmpty()) {
			// TODO: icon
			item->setData(Application::getAppIcon("google"), AddressBarCompleterModel::ImageRole);
		}

		m_items.prepend(item);
	}
}

void AddressBarCompleterRefreshJob::completeFromHistory()
{
	QList<QUrl> urlList;
	// TODO: add this settings in preferences
	Type showType = History;

	if (showType == HistoryAndBookmarks || showType == Bookmarks) {
		// TODO: Bookmarks completion
	}

	std::sort(m_items.begin(), m_items.end(), [](const QStandardItem* item1, const QStandardItem* item2) {
		int i1Count{item1->data(AddressBarCompleterModel::CountRole).toInt()};
		int i2Count{item2->data(AddressBarCompleterModel::CountRole).toInt()};

		return i1Count > i2Count;
	});

	if (showType == HistoryAndBookmarks || showType == History) {
		const int historyLimit{20};
		ndb::sqlite_query<dbs::navigation> query = AddressBarCompleterModel::createHistoryQuery(m_searchString, historyLimit);

		for (auto& entry : query.exec<ndb::objects::history>()) {
			const QUrl url{QUrl(QString::fromStdString(entry.url))};

			if (urlList.contains(url))
				continue;

			QStandardItem* item{new QStandardItem()};
			item->setText(url.toEncoded());
			item->setData(entry.id, AddressBarCompleterModel::IdRole);
			item->setData(QString::fromStdString(entry.title), AddressBarCompleterModel::TitleRole);
			item->setData(url, AddressBarCompleterModel::UrlRole);
			item->setData(entry.count, AddressBarCompleterModel::CountRole);
			item->setData(QVariant(false), AddressBarCompleterModel::BookmarkRole);
			item->setData(m_searchString, AddressBarCompleterModel::SearchStringRole);

			m_items.append(item);
		}
	}
}

void AddressBarCompleterRefreshJob::completeMostVisited()
{
	for (auto& entry : ndb::oquery<dbs::navigation>() << (history << ndb::sort(ndb::desc(history.count)) << ndb::limit(15))
	) {
		QStandardItem* item{new QStandardItem()};
		const QUrl url{QUrl(QString::fromStdString(entry.url))};

		item->setText(url.toEncoded());
		item->setData(entry.id, AddressBarCompleterModel::IdRole);
		item->setData(QString::fromStdString(entry.title), AddressBarCompleterModel::TitleRole);
		item->setData(url, AddressBarCompleterModel::UrlRole);
		item->setData(QVariant(false), AddressBarCompleterModel::BookmarkRole);

		m_items.append(item);
	}
}

QString AddressBarCompleterRefreshJob::createDomainCompletion(const QString& completion) const
{
	if (m_searchString.startsWith(QLatin1String("www.")) && !completion.startsWith(QLatin1String("www.")))
		return QLatin1String("www.") + completion;

	if (!m_searchString.startsWith(QLatin1String("www.")) && completion.startsWith(QLatin1String("www.")))
		return completion.mid(4);

	return completion;
}
}