/* ============================================================
* QupZilla - Qt web browser
* Copyright (C) 2014-2017 David Rosca <nowrep@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* ============================================================ */
#include "tabicon.h"
#include "webtab.h"
#include "webpage.h"
#include "iconprovider.h"
#include "tabbedwebview.h"

#include <QTimer>
#include <QToolTip>
#include <QMouseEvent>

#define ANIMATION_INTERVAL 70

TabIcon::Data *TabIcon::s_data = Q_NULLPTR;

TabIcon::TabIcon(QWidget* parent)
    : QWidget(parent)
    , m_tab(0)
    , m_currentFrame(0)
    , m_animationRunning(false)
    , m_audioIconDisplayed(false)
{
    setObjectName(QSL("tab-icon"));

    if (!s_data) {
        s_data = new TabIcon::Data;
        s_data->animationPixmap = QIcon(QSL(":icons/other/loading.png")).pixmap(288, 16);
        s_data->framesCount = s_data->animationPixmap.width() / s_data->animationPixmap.height();
        s_data->audioPlayingPixmap = QIcon::fromTheme(QSL("audio-volume-high"), QIcon(QSL(":icons/other/audioplaying.svg"))).pixmap(16);
        s_data->audioMutedPixmap = QIcon::fromTheme(QSL("audio-volume-muted"), QIcon(QSL(":icons/other/audiomuted.svg"))).pixmap(16);
    }

    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(ANIMATION_INTERVAL);
    connect(m_updateTimer, SIGNAL(timeout()), this, SLOT(updateAnimationFrame()));

    m_hideTimer = new QTimer(this);
    m_hideTimer->setInterval(250);
    connect(m_hideTimer, &QTimer::timeout, this, &TabIcon::hide);

    resize(16, 16);
}

void TabIcon::setWebTab(WebTab* tab)
{
    m_tab = tab;

    connect(m_tab->webView(), SIGNAL(loadStarted()), this, SLOT(showLoadingAnimation()));
    connect(m_tab->webView(), SIGNAL(loadFinished(bool)), this, SLOT(hideLoadingAnimation()));
    connect(m_tab->webView(), &WebView::iconChanged, this, &TabIcon::updateIcon);
    connect(m_tab->webView()->page(), &QWebEnginePage::recentlyAudibleChanged, this, &TabIcon::updateAudioIcon);

    updateIcon();
}

void TabIcon::showLoadingAnimation()
{
    m_currentFrame = 0;

    updateAnimationFrame();
    show();
}

void TabIcon::hideLoadingAnimation()
{
    m_animationRunning = false;

    m_updateTimer->stop();
    updateIcon();
}

void TabIcon::updateIcon()
{
    m_sitePixmap = m_tab->icon(/*allowNull*/ true).pixmap(16);
    if (m_sitePixmap.isNull()) {
        m_hideTimer->start();
    } else {
        show();
    }
    update();
}

void TabIcon::updateAnimationFrame()
{
    if (!m_animationRunning) {
        m_updateTimer->start();
        m_animationRunning = true;
    }

    update();
    m_currentFrame = (m_currentFrame + 1) % s_data->framesCount;
}

void TabIcon::show()
{
    if (!shouldBeVisible()) {
        return;
    }

    m_hideTimer->stop();

    if (isVisible()) {
        return;
    }

    setFixedSize(16, 16);
    emit resized();
    QWidget::show();
}

void TabIcon::hide()
{
    if (shouldBeVisible() || isHidden()) {
        return;
    }

    emit resized();
    setFixedSize(0, 0);
    QWidget::hide();
}

bool TabIcon::shouldBeVisible() const
{
    if (m_tab && m_tab->isPinned()) {
        return true;
    }
    return !m_sitePixmap.isNull() || m_animationRunning || m_audioIconDisplayed || (m_tab && m_tab->isPinned());
}

bool TabIcon::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *e = static_cast<QHelpEvent*>(event);
        if (m_audioIconDisplayed && m_audioIconRect.contains(e->pos())) {
            QToolTip::showText(e->globalPos(), m_tab->isMuted() ? tr("Unmute Tab") : tr("Mute Tab"), this);
            event->accept();
            return true;
        }
    }

    return QWidget::event(event);
}

void TabIcon::updateAudioIcon(bool recentlyAudible)
{
    if (m_tab->isMuted() || (!m_tab->isMuted() && recentlyAudible)) {
        m_audioIconDisplayed = true;
        show();
    } else {
        m_audioIconDisplayed = false;
        hide();
    }

    update();
}

void TabIcon::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);

    const int size = 16;
    const int pixmapSize = qRound(size * s_data->animationPixmap.devicePixelRatioF());

    // Center the pixmap in rect
    QRect r = rect();
    r.setX((r.width() - size) / 2);
    r.setY((r.height() - size) / 2);
    r.setWidth(size);
    r.setHeight(size);

    if (m_animationRunning) {
        p.drawPixmap(r, s_data->animationPixmap, QRect(m_currentFrame * pixmapSize, 0, pixmapSize, pixmapSize));
    } else if (m_audioIconDisplayed && !m_tab->isPinned()) {
        m_audioIconRect = r;
        p.drawPixmap(r, m_tab->isMuted() ? s_data->audioMutedPixmap : s_data->audioPlayingPixmap);
    } else if (!m_sitePixmap.isNull()) {
        p.drawPixmap(r, m_sitePixmap);
    } else if (m_tab && m_tab->isPinned()) {
        p.drawPixmap(r, IconProvider::emptyWebIcon().pixmap(size));
    }

    // Draw audio icon on top of site icon for pinned tabs
    if (!m_animationRunning && m_audioIconDisplayed && m_tab->isPinned()) {
        const int s = size - 4;
        const QRect r(width() - s, 0, s, s);
        m_audioIconRect = r;
        QColor c = palette().color(QPalette::Window);
        c.setAlpha(180);
        p.setPen(c);
        p.setBrush(c);
        p.drawEllipse(r);
        p.drawPixmap(r, m_tab->isMuted() ? s_data->audioMutedPixmap : s_data->audioPlayingPixmap);
    }
}

void TabIcon::mousePressEvent(QMouseEvent *event)
{
    // If audio icon is clicked - we don't propagate mouse press to the tab
    if (m_audioIconDisplayed && event->button() == Qt::LeftButton && m_audioIconRect.contains(event->pos())) {
        m_tab->toggleMuted();
        return;
    }

    QWidget::mousePressEvent(event);
}
