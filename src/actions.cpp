/***************************************************************************
 *   Copyright (C) 2008-2014 by Andrzej Rybczak                            *
 *   electricityispower@gmail.com                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <cassert>
#include <cerrno>
#include <cstring>
#include <boost/array.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/locale/conversion.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <iostream>
#include <readline/readline.h>

#include "actions.h"
#include "charset.h"
#include "config.h"
#include "display.h"
#include "global.h"
#include "mpdpp.h"
#include "helpers.h"
#include "statusbar.h"
#include "utility/comparators.h"
#include "utility/conversion.h"

#include "bindings.h"
#include "browser.h"
#include "clock.h"
#include "help.h"
#include "media_library.h"
#include "menu_impl.h"
#include "lastfm.h"
#include "lyrics.h"
#include "playlist.h"
#include "playlist_editor.h"
#include "sort_playlist.h"
#include "search_engine.h"
#include "sel_items_adder.h"
#include "server_info.h"
#include "song_info.h"
#include "outputs.h"
#include "utility/string.h"
#include "utility/type_conversions.h"
#include "tag_editor.h"
#include "tiny_tag_editor.h"
#include "visualizer.h"
#include "title.h"
#include "tags.h"

#ifdef HAVE_TAGLIB_H
# include "fileref.h"
# include "tag.h"
#endif // HAVE_TAGLIB_H

using Global::myScreen;

namespace ph = std::placeholders;

namespace {

boost::array<
	Actions::BaseAction *, static_cast<size_t>(Actions::Type::_numberOfActions)
> AvailableActions;

void populateActions();

bool scrollTagCanBeRun(NC::List *&list, SongList *&songs);
void scrollTagUpRun(NC::List *list, SongList *songs, MPD::Song::GetFunction get);
void scrollTagDownRun(NC::List *list, SongList *songs, MPD::Song::GetFunction get);

void seek();
void findItem(const SearchDirection direction);
void listsChangeFinisher();

}

namespace Actions {

bool OriginalStatusbarVisibility;
bool ExitMainLoop = false;

size_t HeaderHeight;
size_t FooterHeight;
size_t FooterStartY;

void validateScreenSize()
{
	using Global::MainHeight;
	
	if (COLS < 30 || MainHeight < 5)
	{
		NC::destroyScreen();
		std::cout << "Screen is too small to handle ncmpcpp correctly\n";
		exit(1);
	}
}

void initializeScreens()
{
	myHelp = new Help;
	myPlaylist = new Playlist;
	myBrowser = new Browser;
	mySearcher = new SearchEngine;
	myLibrary = new MediaLibrary;
	myPlaylistEditor = new PlaylistEditor;
	myLyrics = new Lyrics;
	mySelectedItemsAdder = new SelectedItemsAdder;
	mySongInfo = new SongInfo;
	myServerInfo = new ServerInfo;
	mySortPlaylistDialog = new SortPlaylistDialog;
	
#	ifdef HAVE_CURL_CURL_H
	myLastfm = new Lastfm;
#	endif // HAVE_CURL_CURL_H
	
#	ifdef HAVE_TAGLIB_H
	myTinyTagEditor = new TinyTagEditor;
	myTagEditor = new TagEditor;
#	endif // HAVE_TAGLIB_H
	
#	ifdef ENABLE_VISUALIZER
	myVisualizer = new Visualizer;
#	endif // ENABLE_VISUALIZER
	
#	ifdef ENABLE_OUTPUTS
	myOutputs = new Outputs;
#	endif // ENABLE_OUTPUTS
	
#	ifdef ENABLE_CLOCK
	myClock = new Clock;
#	endif // ENABLE_CLOCK
	
}

void setResizeFlags()
{
	myHelp->hasToBeResized = 1;
	myPlaylist->hasToBeResized = 1;
	myBrowser->hasToBeResized = 1;
	mySearcher->hasToBeResized = 1;
	myLibrary->hasToBeResized = 1;
	myPlaylistEditor->hasToBeResized = 1;
	myLyrics->hasToBeResized = 1;
	mySelectedItemsAdder->hasToBeResized = 1;
	mySongInfo->hasToBeResized = 1;
	myServerInfo->hasToBeResized = 1;
	mySortPlaylistDialog->hasToBeResized = 1;
	
#	ifdef HAVE_CURL_CURL_H
	myLastfm->hasToBeResized = 1;
#	endif // HAVE_CURL_CURL_H
	
#	ifdef HAVE_TAGLIB_H
	myTinyTagEditor->hasToBeResized = 1;
	myTagEditor->hasToBeResized = 1;
#	endif // HAVE_TAGLIB_H
	
#	ifdef ENABLE_VISUALIZER
	myVisualizer->hasToBeResized = 1;
#	endif // ENABLE_VISUALIZER
	
#	ifdef ENABLE_OUTPUTS
	myOutputs->hasToBeResized = 1;
#	endif // ENABLE_OUTPUTS
	
#	ifdef ENABLE_CLOCK
	myClock->hasToBeResized = 1;
#	endif // ENABLE_CLOCK
}

void resizeScreen(bool reload_main_window)
{
	using Global::MainHeight;
	using Global::wHeader;
	using Global::wFooter;

	// update internal screen dimensions
	if (reload_main_window)
	{
		rl_resize_terminal();
		endwin();
		refresh();
	}

	MainHeight = LINES-(Config.design == Design::Alternative ? 7 : 4);

	validateScreenSize();

	if (!Config.header_visibility)
		MainHeight += 2;
	if (!Config.statusbar_visibility)
		++MainHeight;

	setResizeFlags();

	applyToVisibleWindows(&BaseScreen::resize);

	if (Config.header_visibility || Config.design == Design::Alternative)
		wHeader->resize(COLS, HeaderHeight);

	FooterStartY = LINES-(Config.statusbar_visibility ? 2 : 1);
	wFooter->moveTo(0, FooterStartY);
	wFooter->resize(COLS, Config.statusbar_visibility ? 2 : 1);

	applyToVisibleWindows(&BaseScreen::refresh);

	Status::Changes::elapsedTime(false);
	Status::Changes::playerState();
	// Note: routines for drawing separator if alternative user
	// interface is active and header is hidden are placed in
	// NcmpcppStatusChanges.StatusFlags
	Status::Changes::flags();
	drawHeader();
	wFooter->refresh();
	refresh();
}

void setWindowsDimensions()
{
	using Global::MainStartY;
	using Global::MainHeight;
	
	MainStartY = Config.design == Design::Alternative ? 5 : 2;
	MainHeight = LINES-(Config.design == Design::Alternative ? 7 : 4);
	
	if (!Config.header_visibility)
	{
		MainStartY -= 2;
		MainHeight += 2;
	}
	if (!Config.statusbar_visibility)
		++MainHeight;
	
	HeaderHeight = Config.design == Design::Alternative ? (Config.header_visibility ? 5 : 3) : 1;
	FooterStartY = LINES-(Config.statusbar_visibility ? 2 : 1);
	FooterHeight = Config.statusbar_visibility ? 2 : 1;
}

void confirmAction(const boost::format &description)
{
	Statusbar::ScopedLock slock;
	Statusbar::put() << description.str()
	<< " [" << NC::Format::Bold << 'y' << NC::Format::NoBold
	<< '/' << NC::Format::Bold << 'n' << NC::Format::NoBold
	<< "] ";
	auto answer = Statusbar::Helpers::promptReturnOneOf({"y", "n"});
	if (answer == "n")
		throw NC::PromptAborted(std::move(answer));
}

bool isMPDMusicDirSet()
{
	if (Config.mpd_music_dir.empty())
	{
		Statusbar::print("Proper mpd_music_dir variable has to be set in configuration file");
		return false;
	}
	return true;
}

BaseAction &get(Actions::Type at)
{
	if (AvailableActions[1] == nullptr)
		populateActions();
	BaseAction *action = AvailableActions[static_cast<size_t>(at)];
	// action should be always present if action type in queried
	assert(action != nullptr);
	return *action;
}

BaseAction *get(const std::string &name)
{
	BaseAction *result = 0;
	if (AvailableActions[1] == nullptr)
		populateActions();
	for (auto it = AvailableActions.begin(); it != AvailableActions.end(); ++it)
	{
		if (*it != nullptr && (*it)->name() == name)
		{
			result = *it;
			break;
		}
	}
	return result;
}

UpdateEnvironment::UpdateEnvironment()
: BaseAction(Type::UpdateEnvironment, "update_environment")
, m_past(boost::posix_time::from_time_t(0))
{ }

void UpdateEnvironment::run(bool update_timer, bool refresh_window)
{
	using Global::Timer;

	// update timer, status if necessary etc.
	Status::trace(update_timer, true);

	// header stuff
	if ((myScreen == myPlaylist || myScreen == myBrowser || myScreen == myLyrics)
	&&  (Timer - m_past > boost::posix_time::milliseconds(500))
	)
	{
		drawHeader();
		m_past = Timer;
	}

	if (refresh_window)
		myScreen->refreshWindow();
}

void UpdateEnvironment::run()
{
	run(true, true);
}

bool MouseEvent::canBeRun()
{
	return Config.mouse_support;
}

void MouseEvent::run()
{
	using Global::VolumeState;
	using Global::wFooter;

	m_old_mouse_event = m_mouse_event;
	m_mouse_event = wFooter->getMouseEvent();

	//Statusbar::printf("(%1%, %2%, %3%)", m_mouse_event.bstate, m_mouse_event.x, m_mouse_event.y);

	if (m_mouse_event.bstate & BUTTON1_PRESSED
	&&  m_mouse_event.y == LINES-(Config.statusbar_visibility ? 2 : 1)
	   ) // progressbar
	{
		if (Status::State::player() == MPD::psStop)
			return;
		Mpd.Seek(Status::State::currentSongPosition(),
			Status::State::totalTime()*m_mouse_event.x/double(COLS));
	}
	else if (m_mouse_event.bstate & BUTTON1_PRESSED
	     &&  (Config.statusbar_visibility || Config.design == Design::Alternative)
	     &&  Status::State::player() != MPD::psStop
	     &&  m_mouse_event.y == (Config.design == Design::Alternative ? 1 : LINES-1)
			 &&  m_mouse_event.x < 9
		) // playing/paused
	{
		Mpd.Toggle();
	}
	else if ((m_mouse_event.bstate & BUTTON5_PRESSED || m_mouse_event.bstate & BUTTON4_PRESSED)
	     &&	 (Config.header_visibility || Config.design == Design::Alternative)
	     &&	 m_mouse_event.y == 0 && size_t(m_mouse_event.x) > COLS-VolumeState.length()
	) // volume
	{
		if (m_mouse_event.bstate & BUTTON5_PRESSED)
			get(Type::VolumeDown).execute();
		else
			get(Type::VolumeUp).execute();
	}
	else if (m_mouse_event.bstate & (BUTTON1_PRESSED | BUTTON3_PRESSED | BUTTON4_PRESSED | BUTTON5_PRESSED))
		myScreen->mouseButtonPressed(m_mouse_event);
}

void ScrollUp::run()
{
	myScreen->scroll(NC::Scroll::Up);
	listsChangeFinisher();
}

void ScrollDown::run()
{
	myScreen->scroll(NC::Scroll::Down);
	listsChangeFinisher();
}

bool ScrollUpArtist::canBeRun()
{
	return scrollTagCanBeRun(m_list, m_songs);
}

void ScrollUpArtist::run()
{
	scrollTagUpRun(m_list, m_songs, &MPD::Song::getArtist);
}

bool ScrollUpAlbum::canBeRun()
{
	return scrollTagCanBeRun(m_list, m_songs);
}

void ScrollUpAlbum::run()
{
	scrollTagUpRun(m_list, m_songs, &MPD::Song::getAlbum);
}

bool ScrollDownArtist::canBeRun()
{
	return scrollTagCanBeRun(m_list, m_songs);
}

void ScrollDownArtist::run()
{
	scrollTagDownRun(m_list, m_songs, &MPD::Song::getArtist);
}

bool ScrollDownAlbum::canBeRun()
{
	return scrollTagCanBeRun(m_list, m_songs);
}

void ScrollDownAlbum::run()
{
	scrollTagDownRun(m_list, m_songs, &MPD::Song::getAlbum);
}

void PageUp::run()
{
	myScreen->scroll(NC::Scroll::PageUp);
	listsChangeFinisher();
}

void PageDown::run()
{
	myScreen->scroll(NC::Scroll::PageDown);
	listsChangeFinisher();
}

void MoveHome::run()
{
	myScreen->scroll(NC::Scroll::Home);
	listsChangeFinisher();
}

void MoveEnd::run()
{
	myScreen->scroll(NC::Scroll::End);
	listsChangeFinisher();
}

void ToggleInterface::run()
{
	switch (Config.design)
	{
		case Design::Classic:
			Config.design = Design::Alternative;
			Config.statusbar_visibility = false;
			break;
		case Design::Alternative:
			Config.design = Design::Classic;
			Config.statusbar_visibility = OriginalStatusbarVisibility;
			break;
	}
	setWindowsDimensions();
	resizeScreen(false);
	// unlock progressbar
	Progressbar::ScopedLock();
	Status::Changes::mixer();
	Status::Changes::elapsedTime(false);
	Statusbar::printf("User interface: %1%", Config.design);
}

bool JumpToParentDirectory::canBeRun()
{
	return (myScreen == myBrowser)
#	ifdef HAVE_TAGLIB_H
	    || (myScreen->activeWindow() == myTagEditor->Dirs)
#	endif // HAVE_TAGLIB_H
	;
}

void JumpToParentDirectory::run()
{
	if (myScreen == myBrowser)
	{
		if (!myBrowser->inRootDirectory())
		{
			myBrowser->main().reset();
			myBrowser->enterPressed();
		}
	}
#	ifdef HAVE_TAGLIB_H
	else if (myScreen == myTagEditor)
	{
		if (myTagEditor->CurrentDir() != "/")
		{
			myTagEditor->Dirs->reset();
			myTagEditor->enterPressed();
		}
	}
#	endif // HAVE_TAGLIB_H
}

void PressEnter::run()
{
	myScreen->enterPressed();
}

bool PreviousColumn::canBeRun()
{
	auto hc = hasColumns(myScreen);
	return hc && hc->previousColumnAvailable();
}

void PreviousColumn::run()
{
	hasColumns(myScreen)->previousColumn();
}

bool NextColumn::canBeRun()
{
	auto hc = hasColumns(myScreen);
	return hc && hc->nextColumnAvailable();
}

void NextColumn::run()
{
	hasColumns(myScreen)->nextColumn();
}

bool MasterScreen::canBeRun()
{
	using Global::myLockedScreen;
	using Global::myInactiveScreen;
	
	return myLockedScreen
	    && myInactiveScreen
	    && myLockedScreen != myScreen
	    && myScreen->isMergable();
}

void MasterScreen::run()
{
	using Global::myInactiveScreen;
	using Global::myLockedScreen;
	
	myInactiveScreen = myScreen;
	myScreen = myLockedScreen;
	drawHeader();
}

bool SlaveScreen::canBeRun()
{
	using Global::myLockedScreen;
	using Global::myInactiveScreen;
	
	return myLockedScreen
	    && myInactiveScreen
	    && myLockedScreen == myScreen
	    && myScreen->isMergable();
}

void SlaveScreen::run()
{
	using Global::myInactiveScreen;
	using Global::myLockedScreen;
	
	myScreen = myInactiveScreen;
	myInactiveScreen = myLockedScreen;
	drawHeader();
}

void VolumeUp::run()
{
	int volume = std::min(Status::State::volume()+Config.volume_change_step, 100u);
	Mpd.SetVolume(volume);
}

void VolumeDown::run()
{
	int volume = std::max(int(Status::State::volume()-Config.volume_change_step), 0);
	Mpd.SetVolume(volume);
}

bool AddItemToPlaylist::canBeRun()
{
	if (m_hs != static_cast<void *>(myScreen))
		m_hs = dynamic_cast<HasSongs *>(myScreen);
	return m_hs != nullptr;
}

void AddItemToPlaylist::run()
{
	bool success = m_hs->addItemToPlaylist();
	if (success)
	{
		myScreen->scroll(NC::Scroll::Down);
		listsChangeFinisher();
	}
}

bool DeletePlaylistItems::canBeRun()
{
	return (myScreen == myPlaylist && !myPlaylist->main().empty())
	    || (myScreen->isActiveWindow(myPlaylistEditor->Content) && !myPlaylistEditor->Content.empty());
}

void DeletePlaylistItems::run()
{
	if (myScreen == myPlaylist)
	{
		Statusbar::print("Deleting items...");
		auto delete_fun = std::bind(&MPD::Connection::Delete, ph::_1, ph::_2);
		deleteSelectedSongs(myPlaylist->main(), delete_fun);
		Statusbar::print("Item(s) deleted");
	}
	else if (myScreen->isActiveWindow(myPlaylistEditor->Content))
	{
		std::string playlist = myPlaylistEditor->Playlists.current()->value().path();
		auto delete_fun = std::bind(&MPD::Connection::PlaylistDelete, ph::_1, playlist, ph::_2);
		Statusbar::print("Deleting items...");
		deleteSelectedSongs(myPlaylistEditor->Content, delete_fun);
		Statusbar::print("Item(s) deleted");
	}
}

bool DeleteBrowserItems::canBeRun()
{
	auto check_if_deletion_allowed = []() {
		if (Config.allow_for_physical_item_deletion)
			return true;
		else
		{
			Statusbar::print("Flag \"allow_for_physical_item_deletion\" needs to be enabled in configuration file");
			return false;
		}
	};
	return myScreen == myBrowser
	    && !myBrowser->main().empty()
	    && isMPDMusicDirSet()
	    && check_if_deletion_allowed();
}

void DeleteBrowserItems::run()
{
	auto get_name = [](const MPD::Item &item) -> std::string {
		std::string iname;
		switch (item.type())
		{
			case MPD::Item::Type::Directory:
				iname = getBasename(item.directory().path());
				break;
			case MPD::Item::Type::Song:
				iname = item.song().getName();
				break;
			case MPD::Item::Type::Playlist:
				iname = getBasename(item.playlist().path());
				break;
		}
		return iname;
	};

	boost::format question;
	if (hasSelected(myBrowser->main().begin(), myBrowser->main().end()))
		question = boost::format("Delete selected items?");
	else
	{
		const auto &item = myBrowser->main().current()->value();
		// parent directories are not accepted (and they
		// can't be selected, so in other cases it's fine).
		if (myBrowser->isParentDirectory(item))
			return;
		const char msg[] = "Delete \"%1%\"?";
		question = boost::format(msg) % wideShorten(
			get_name(item), COLS-const_strlen(msg)-5
		);
	}
	confirmAction(question);

	auto items = getSelectedOrCurrent(
		myBrowser->main().begin(),
		myBrowser->main().end(),
		myBrowser->main().current()
	);
	for (const auto &item : items)
	{
		myBrowser->remove(item->value());
		const char msg[] = "Deleted %1% \"%2%\"";
		Statusbar::printf(msg,
			itemTypeToString(item->value().type()),
			wideShorten(get_name(item->value()), COLS-const_strlen(msg))
		);
	}

	if (!myBrowser->isLocal())
		Mpd.UpdateDirectory(myBrowser->currentDirectory());
	myBrowser->requestUpdate();
}

bool DeleteStoredPlaylist::canBeRun()
{
	return myScreen->isActiveWindow(myPlaylistEditor->Playlists);
}

void DeleteStoredPlaylist::run()
{
	if (myPlaylistEditor->Playlists.empty())
		return;
	boost::format question;
	if (hasSelected(myPlaylistEditor->Playlists.begin(), myPlaylistEditor->Playlists.end()))
		question = boost::format("Delete selected playlists?");
	else
		question = boost::format("Delete playlist \"%1%\"?")
			% wideShorten(myPlaylistEditor->Playlists.current()->value().path(), COLS-question.size()-10);
	confirmAction(question);
	auto list = getSelectedOrCurrent(
		myPlaylistEditor->Playlists.begin(),
		myPlaylistEditor->Playlists.end(),
		myPlaylistEditor->Playlists.current()
	);
	for (const auto &item : list)
		Mpd.DeletePlaylist(item->value().path());
	Statusbar::printf("%1% deleted", list.size() == 1 ? "Playlist" : "Playlists");
	// force playlists update. this happens automatically, but only after call
	// to Key::read, therefore when we call PlaylistEditor::Update, it won't
	// yet see it, so let's point that it needs to update it.
	myPlaylistEditor->requestPlaylistsUpdate();
}

void ReplaySong::run()
{
	if (Status::State::player() != MPD::psStop)
		Mpd.Seek(Status::State::currentSongPosition(), 0);
}

void PreviousSong::run()
{
	Mpd.Prev();
}

void NextSong::run()
{
	Mpd.Next();
}

void Pause::run()
{
	Mpd.Toggle();
}

void SavePlaylist::run()
{
	using Global::wFooter;
	
	std::string playlist_name;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Save playlist as: ";
		playlist_name = wFooter->prompt();
	}
	try
	{
		Mpd.SavePlaylist(playlist_name);
		Statusbar::printf("Playlist saved as \"%1%\"", playlist_name);
	}
	catch (MPD::ServerError &e)
	{
		if (e.code() == MPD_SERVER_ERROR_EXIST)
		{
			confirmAction(
				boost::format("Playlist \"%1%\" already exists, overwrite?") % playlist_name
			);
			Mpd.DeletePlaylist(playlist_name);
			Mpd.SavePlaylist(playlist_name);
			Statusbar::print("Playlist overwritten");
		}
		else
			throw;
	}
}

void Stop::run()
{
	Mpd.Stop();
}

void ExecuteCommand::run()
{
	using Global::wFooter;

	std::string cmd_name;
	{
		Statusbar::ScopedLock slock;
		NC::Window::ScopedPromptHook helper(*wFooter,
			Statusbar::Helpers::TryExecuteImmediateCommand()
		);
		Statusbar::put() << NC::Format::Bold << ":" << NC::Format::NoBold;
		cmd_name = wFooter->prompt();
	}

	auto cmd = Bindings.findCommand(cmd_name);
	if (cmd)
	{
		Statusbar::printf(1, "Executing %1%...", cmd_name);
		bool res = cmd->binding().execute();
		Statusbar::printf("Execution of command \"%1%\" %2%.",
			cmd_name, res ? "successful" : "unsuccessful"
		);
	}
	else
		Statusbar::printf("No command named \"%1%\"", cmd_name);
}

bool MoveSortOrderUp::canBeRun()
{
	return myScreen == mySortPlaylistDialog;
}

void MoveSortOrderUp::run()
{
	mySortPlaylistDialog->moveSortOrderUp();
}

bool MoveSortOrderDown::canBeRun()
{
	return myScreen == mySortPlaylistDialog;
}

void MoveSortOrderDown::run()
{
	mySortPlaylistDialog->moveSortOrderDown();
}

bool MoveSelectedItemsUp::canBeRun()
{
	return ((myScreen == myPlaylist
	    &&  !myPlaylist->main().empty())
	 ||    (myScreen->isActiveWindow(myPlaylistEditor->Content)
	    &&  !myPlaylistEditor->Content.empty()));
}

void MoveSelectedItemsUp::run()
{
	if (myScreen == myPlaylist)
	{
		moveSelectedItemsUp(myPlaylist->main(), std::bind(&MPD::Connection::Move, ph::_1, ph::_2, ph::_3));
	}
	else if (myScreen == myPlaylistEditor)
	{
		assert(!myPlaylistEditor->Playlists.empty());
		std::string playlist = myPlaylistEditor->Playlists.current()->value().path();
		auto move_fun = std::bind(&MPD::Connection::PlaylistMove, ph::_1, playlist, ph::_2, ph::_3);
		moveSelectedItemsUp(myPlaylistEditor->Content, move_fun);
	}
}

bool MoveSelectedItemsDown::canBeRun()
{
	return ((myScreen == myPlaylist
	    &&  !myPlaylist->main().empty())
	 ||    (myScreen->isActiveWindow(myPlaylistEditor->Content)
	    &&  !myPlaylistEditor->Content.empty()));
}

void MoveSelectedItemsDown::run()
{
	if (myScreen == myPlaylist)
	{
		moveSelectedItemsDown(myPlaylist->main(), std::bind(&MPD::Connection::Move, ph::_1, ph::_2, ph::_3));
	}
	else if (myScreen == myPlaylistEditor)
	{
		assert(!myPlaylistEditor->Playlists.empty());
		std::string playlist = myPlaylistEditor->Playlists.current()->value().path();
		auto move_fun = std::bind(&MPD::Connection::PlaylistMove, ph::_1, playlist, ph::_2, ph::_3);
		moveSelectedItemsDown(myPlaylistEditor->Content, move_fun);
	}
}

bool MoveSelectedItemsTo::canBeRun()
{
	return myScreen == myPlaylist
	    || myScreen->isActiveWindow(myPlaylistEditor->Content);
}

void MoveSelectedItemsTo::run()
{
	if (myScreen == myPlaylist)
	{
		if (!myPlaylist->main().empty())
			moveSelectedItemsTo(myPlaylist->main(), std::bind(&MPD::Connection::Move, ph::_1, ph::_2, ph::_3));
	}
	else
	{
		assert(!myPlaylistEditor->Playlists.empty());
		std::string playlist = myPlaylistEditor->Playlists.current()->value().path();
		auto move_fun = std::bind(&MPD::Connection::PlaylistMove, ph::_1, playlist, ph::_2, ph::_3);
		moveSelectedItemsTo(myPlaylistEditor->Content, move_fun);
	}
}

bool Add::canBeRun()
{
	return myScreen != myPlaylistEditor
	   || !myPlaylistEditor->Playlists.empty();
}

void Add::run()
{
	using Global::wFooter;
	
	std::string path;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << (myScreen == myPlaylistEditor ? "Add to playlist: " : "Add: ");
		path = wFooter->prompt();
	}

	// confirm when one wants to add the whole database
	if (path.empty())
		confirmAction("Are you sure you want to add the whole database?");

	Statusbar::put() << "Adding...";
	wFooter->refresh();
	if (myScreen == myPlaylistEditor)
		Mpd.AddToPlaylist(myPlaylistEditor->Playlists.current()->value().path(), path);
	else
	{
		try
		{
			Mpd.Add(path);
		}
		catch (MPD::ServerError &err)
		{
			// If a path is not a file or directory, assume it is a playlist.
			if (err.code() == MPD_SERVER_ERROR_NO_EXIST)
				Mpd.LoadPlaylist(path);
			else
				throw;
		}
	}
}

bool SeekForward::canBeRun()
{
	return Status::State::player() != MPD::psStop && Status::State::totalTime() > 0;
}

void SeekForward::run()
{
	seek();
}

bool SeekBackward::canBeRun()
{
	return Status::State::player() != MPD::psStop && Status::State::totalTime() > 0;
}

void SeekBackward::run()
{
	seek();
}

bool ToggleDisplayMode::canBeRun()
{
	return myScreen == myPlaylist
	    || myScreen == myBrowser
	    || myScreen == mySearcher
	    || myScreen->isActiveWindow(myPlaylistEditor->Content);
}

void ToggleDisplayMode::run()
{
	if (myScreen == myPlaylist)
	{
		switch (Config.playlist_display_mode)
		{
			case DisplayMode::Classic:
				Config.playlist_display_mode = DisplayMode::Columns;
				myPlaylist->main().setItemDisplayer(std::bind(
					Display::SongsInColumns, ph::_1, std::cref(myPlaylist->main())
				));
				if (Config.titles_visibility)
					myPlaylist->main().setTitle(Display::Columns(myPlaylist->main().getWidth()));
				else
					myPlaylist->main().setTitle("");
				break;
			case DisplayMode::Columns:
				Config.playlist_display_mode = DisplayMode::Classic;
				myPlaylist->main().setItemDisplayer(std::bind(
					Display::Songs, ph::_1, std::cref(myPlaylist->main()), std::cref(Config.song_list_format)
				));
				myPlaylist->main().setTitle("");
		}
		Statusbar::printf("Playlist display mode: %1%", Config.playlist_display_mode);
	}
	else if (myScreen == myBrowser)
	{
		switch (Config.browser_display_mode)
		{
			case DisplayMode::Classic:
				Config.browser_display_mode = DisplayMode::Columns;
				if (Config.titles_visibility)
					myBrowser->main().setTitle(Display::Columns(myBrowser->main().getWidth()));
				else
					myBrowser->main().setTitle("");
				break;
			case DisplayMode::Columns:
				Config.browser_display_mode = DisplayMode::Classic;
				myBrowser->main().setTitle("");
				break;
		}
		Statusbar::printf("Browser display mode: %1%", Config.browser_display_mode);
	}
	else if (myScreen == mySearcher)
	{
		switch (Config.search_engine_display_mode)
		{
			case DisplayMode::Classic:
				Config.search_engine_display_mode = DisplayMode::Columns;
				break;
			case DisplayMode::Columns:
				Config.search_engine_display_mode = DisplayMode::Classic;
				break;
		}
		Statusbar::printf("Search engine display mode: %1%", Config.search_engine_display_mode);
		if (mySearcher->main().size() > SearchEngine::StaticOptions)
			mySearcher->main().setTitle(
				   Config.search_engine_display_mode == DisplayMode::Columns
				&& Config.titles_visibility
				? Display::Columns(mySearcher->main().getWidth())
				: ""
			);
	}
	else if (myScreen->isActiveWindow(myPlaylistEditor->Content))
	{
		switch (Config.playlist_editor_display_mode)
		{
			case DisplayMode::Classic:
				Config.playlist_editor_display_mode = DisplayMode::Columns;
				myPlaylistEditor->Content.setItemDisplayer(std::bind(
					Display::SongsInColumns, ph::_1, std::cref(myPlaylistEditor->Content)
				));
				break;
			case DisplayMode::Columns:
				Config.playlist_editor_display_mode = DisplayMode::Classic;
				myPlaylistEditor->Content.setItemDisplayer(std::bind(
					Display::Songs, ph::_1, std::cref(myPlaylistEditor->Content), std::cref(Config.song_list_format)
				));
				break;
		}
		Statusbar::printf("Playlist editor display mode: %1%", Config.playlist_editor_display_mode);
	}
}

bool ToggleSeparatorsBetweenAlbums::canBeRun()
{
	return true;
}

void ToggleSeparatorsBetweenAlbums::run()
{
	Config.playlist_separate_albums = !Config.playlist_separate_albums;
	Statusbar::printf("Separators between albums: %1%",
		Config.playlist_separate_albums ? "on" : "off"
	);
}

bool ToggleLyricsUpdateOnSongChange::canBeRun()
{
	return myScreen == myLyrics;
}

void ToggleLyricsUpdateOnSongChange::run()
{
	Config.now_playing_lyrics = !Config.now_playing_lyrics;
	Statusbar::printf("Update lyrics if song changes: %1%",
		Config.now_playing_lyrics ? "on" : "off"
	);
}

#ifndef HAVE_CURL_CURL_H
bool ToggleLyricsFetcher::canBeRun()
{
	return false;
}
#endif // NOT HAVE_CURL_CURL_H

void ToggleLyricsFetcher::run()
{
#	ifdef HAVE_CURL_CURL_H
	myLyrics->ToggleFetcher();
#	endif // HAVE_CURL_CURL_H
}

#ifndef HAVE_CURL_CURL_H
bool ToggleFetchingLyricsInBackground::canBeRun()
{
	return false;
}
#endif // NOT HAVE_CURL_CURL_H

void ToggleFetchingLyricsInBackground::run()
{
#	ifdef HAVE_CURL_CURL_H
	Config.fetch_lyrics_in_background = !Config.fetch_lyrics_in_background;
	Statusbar::printf("Fetching lyrics for playing songs in background: %1%",
		Config.fetch_lyrics_in_background ? "on" : "off"
	);
#	endif // HAVE_CURL_CURL_H
}

void TogglePlayingSongCentering::run()
{
	Config.autocenter_mode = !Config.autocenter_mode;
	Statusbar::printf("Centering playing song: %1%",
		Config.autocenter_mode ? "on" : "off"
	);
	if (Config.autocenter_mode)
	{
		auto s = myPlaylist->nowPlayingSong();
		if (!s.empty())
			myPlaylist->main().highlight(s.getPosition());
	}
}

void UpdateDatabase::run()
{
	if (myScreen == myBrowser)
		Mpd.UpdateDirectory(myBrowser->currentDirectory());
#	ifdef HAVE_TAGLIB_H
	else if (myScreen == myTagEditor)
		Mpd.UpdateDirectory(myTagEditor->CurrentDir());
#	endif // HAVE_TAGLIB_H
	else
		Mpd.UpdateDirectory("/");
}

bool JumpToPlayingSong::canBeRun()
{
	return myScreen == myPlaylist
	    || myScreen == myBrowser
	    || myScreen == myLibrary;
}

void JumpToPlayingSong::run()
{
	auto s = myPlaylist->nowPlayingSong();
	if (s.empty())
		return;
	if (myScreen == myPlaylist)
	{
		myPlaylist->main().highlight(s.getPosition());
	}
	else if (myScreen == myBrowser)
	{
		myBrowser->locateSong(s);
	}
	else if (myScreen == myLibrary)
	{
		myLibrary->LocateSong(s);
	}
}

void ToggleRepeat::run()
{
	Mpd.SetRepeat(!Status::State::repeat());
}

void Shuffle::run()
{
	auto begin = myPlaylist->main().begin(), end = myPlaylist->main().end();
	auto range = getSelectedRange(begin, end);
	Mpd.ShuffleRange(range.first-begin, range.second-begin);
	Statusbar::print("Range shuffled");
}

void ToggleRandom::run()
{
	Mpd.SetRandom(!Status::State::random());
}

bool StartSearching::canBeRun()
{
	return myScreen == mySearcher && !mySearcher->main()[0].isInactive();
}

void StartSearching::run()
{
	mySearcher->main().highlight(SearchEngine::SearchButton);
	mySearcher->main().setHighlighting(0);
	mySearcher->main().refresh();
	mySearcher->main().setHighlighting(1);
	mySearcher->enterPressed();
}

bool SaveTagChanges::canBeRun()
{
#	ifdef HAVE_TAGLIB_H
	return myScreen == myTinyTagEditor
	    || myScreen->activeWindow() == myTagEditor->TagTypes;
#	else
	return false;
#	endif // HAVE_TAGLIB_H
}

void SaveTagChanges::run()
{
#	ifdef HAVE_TAGLIB_H
	if (myScreen == myTinyTagEditor)
	{
		myTinyTagEditor->main().highlight(myTinyTagEditor->main().size()-2); // Save
		myTinyTagEditor->enterPressed();
	}
	else if (myScreen->activeWindow() == myTagEditor->TagTypes)
	{
		myTagEditor->TagTypes->highlight(myTagEditor->TagTypes->size()-1); // Save
		myTagEditor->enterPressed();
	}
#	endif // HAVE_TAGLIB_H
}

void ToggleSingle::run()
{
	Mpd.SetSingle(!Status::State::single());
}

void ToggleConsume::run()
{
	Mpd.SetConsume(!Status::State::consume());
}

void ToggleCrossfade::run()
{
	Mpd.SetCrossfade(Status::State::crossfade() ? 0 : Config.crossfade_time);
}

void SetCrossfade::run()
{
	using Global::wFooter;
	
	Statusbar::ScopedLock slock;
	Statusbar::put() << "Set crossfade to: ";
	auto crossfade = fromString<unsigned>(wFooter->prompt());
	lowerBoundCheck(crossfade, 0u);
	Config.crossfade_time = crossfade;
	Mpd.SetCrossfade(crossfade);
}

void SetVolume::run()
{
	using Global::wFooter;
	
	unsigned volume;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Set volume to: ";
		volume = fromString<unsigned>(wFooter->prompt());
		boundsCheck(volume, 0u, 100u);
		Mpd.SetVolume(volume);
	}
	Statusbar::printf("Volume set to %1%%%", volume);
}

bool EditSong::canBeRun()
{
#	ifdef HAVE_TAGLIB_H
	m_song = currentSong(myScreen);
	return m_song != nullptr && isMPDMusicDirSet();
#	else
	return false;
#	endif // HAVE_TAGLIB_H
}

void EditSong::run()
{
#	ifdef HAVE_TAGLIB_H
	myTinyTagEditor->SetEdited(*m_song);
	myTinyTagEditor->switchTo();
#	endif // HAVE_TAGLIB_H
}

bool EditLibraryTag::canBeRun()
{
#	ifdef HAVE_TAGLIB_H
	return myScreen->isActiveWindow(myLibrary->Tags)
	    && !myLibrary->Tags.empty()
	    && isMPDMusicDirSet();
#	else
	return false;
#	endif // HAVE_TAGLIB_H
}

void EditLibraryTag::run()
{
#	ifdef HAVE_TAGLIB_H
	using Global::wFooter;

	std::string new_tag;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << NC::Format::Bold << tagTypeToString(Config.media_lib_primary_tag) << NC::Format::NoBold << ": ";
		new_tag = wFooter->prompt(myLibrary->Tags.current()->value().tag());
	}
	if (!new_tag.empty() && new_tag != myLibrary->Tags.current()->value().tag())
	{
		Statusbar::print("Updating tags...");
		Mpd.StartSearch(true);
		Mpd.AddSearch(Config.media_lib_primary_tag, myLibrary->Tags.current()->value().tag());
		MPD::MutableSong::SetFunction set = tagTypeToSetFunction(Config.media_lib_primary_tag);
		assert(set);
		bool success = true;
		std::string dir_to_update;
		for (MPD::SongIterator s = Mpd.CommitSearchSongs(), end; s != end; ++s)
		{
			MPD::MutableSong ms = std::move(*s);
			ms.setTags(set, new_tag);
			Statusbar::printf("Updating tags in \"%1%\"...", ms.getName());
			std::string path = Config.mpd_music_dir + ms.getURI();
			if (!Tags::write(ms))
			{
				success = false;
				const char msg[] = "Error while updating tags in \"%1%\"";
				Statusbar::printf(msg, wideShorten(ms.getURI(), COLS-const_strlen(msg)));
				s.finish();
				break;
			}
			if (dir_to_update.empty())
				dir_to_update = ms.getURI();
			else
				dir_to_update = getSharedDirectory(dir_to_update, ms.getURI());
		};
		if (success)
		{
			Mpd.UpdateDirectory(dir_to_update);
			Statusbar::print("Tags updated successfully");
		}
	}
#	endif // HAVE_TAGLIB_H
}

bool EditLibraryAlbum::canBeRun()
{
#	ifdef HAVE_TAGLIB_H
	return myScreen->isActiveWindow(myLibrary->Albums)
	    && !myLibrary->Albums.empty()
		&& isMPDMusicDirSet();
#	else
	return false;
#	endif // HAVE_TAGLIB_H
}

void EditLibraryAlbum::run()
{
#	ifdef HAVE_TAGLIB_H
	using Global::wFooter;
	// FIXME: merge this and EditLibraryTag. also, prompt on failure if user wants to continue
	std::string new_album;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << NC::Format::Bold << "Album: " << NC::Format::NoBold;
		new_album = wFooter->prompt(myLibrary->Albums.current()->value().entry().album());
	}
	if (!new_album.empty() && new_album != myLibrary->Albums.current()->value().entry().album())
	{
		bool success = 1;
		Statusbar::print("Updating tags...");
		for (size_t i = 0;  i < myLibrary->Songs.size(); ++i)
		{
			Statusbar::printf("Updating tags in \"%1%\"...", myLibrary->Songs[i].value().getName());
			std::string path = Config.mpd_music_dir + myLibrary->Songs[i].value().getURI();
			TagLib::FileRef f(path.c_str());
			if (f.isNull())
			{
				const char msg[] = "Error while opening file \"%1%\"";
				Statusbar::printf(msg, wideShorten(myLibrary->Songs[i].value().getURI(), COLS-const_strlen(msg)));
				success = 0;
				break;
			}
			f.tag()->setAlbum(ToWString(new_album));
			if (!f.save())
			{
				const char msg[] = "Error while writing tags in \"%1%\"";
				Statusbar::printf(msg, wideShorten(myLibrary->Songs[i].value().getURI(), COLS-const_strlen(msg)));
				success = 0;
				break;
			}
		}
		if (success)
		{
			Mpd.UpdateDirectory(getSharedDirectory(myLibrary->Songs.beginV(), myLibrary->Songs.endV()));
			Statusbar::print("Tags updated successfully");
		}
	}
#	endif // HAVE_TAGLIB_H
}

bool EditDirectoryName::canBeRun()
{
	return  ((myScreen == myBrowser
	      && !myBrowser->main().empty()
	      && myBrowser->main().current()->value().type() == MPD::Item::Type::Directory)
#	ifdef HAVE_TAGLIB_H
	    ||   (myScreen->activeWindow() == myTagEditor->Dirs
	      && !myTagEditor->Dirs->empty()
	      && myTagEditor->Dirs->choice() > 0)
#	endif // HAVE_TAGLIB_H
	) &&     isMPDMusicDirSet();
}

void EditDirectoryName::run()
{
	using Global::wFooter;
	if (myScreen == myBrowser)
	{
		std::string old_dir = myBrowser->main().current()->value().directory().path();
		std::string new_dir;
		{
			Statusbar::ScopedLock slock;
			Statusbar::put() << NC::Format::Bold << "Directory: " << NC::Format::NoBold;
			new_dir = wFooter->prompt(old_dir);
		}
		if (!new_dir.empty() && new_dir != old_dir)
		{
			std::string full_old_dir;
			if (!myBrowser->isLocal())
				full_old_dir += Config.mpd_music_dir;
			full_old_dir += old_dir;
			std::string full_new_dir;
			if (!myBrowser->isLocal())
				full_new_dir += Config.mpd_music_dir;
			full_new_dir += new_dir;
			boost::filesystem::rename(full_old_dir, full_new_dir);
			const char msg[] = "Directory renamed to \"%1%\"";
			Statusbar::printf(msg, wideShorten(new_dir, COLS-const_strlen(msg)));
			if (!myBrowser->isLocal())
				Mpd.UpdateDirectory(getSharedDirectory(old_dir, new_dir));
			myBrowser->requestUpdate();
		}
	}
#	ifdef HAVE_TAGLIB_H
	else if (myScreen->activeWindow() == myTagEditor->Dirs)
	{
		std::string old_dir = myTagEditor->Dirs->current()->value().first, new_dir;
		{
			Statusbar::ScopedLock slock;
			Statusbar::put() << NC::Format::Bold << "Directory: " << NC::Format::NoBold;
			new_dir = wFooter->prompt(old_dir);
		}
		if (!new_dir.empty() && new_dir != old_dir)
		{
			std::string full_old_dir = Config.mpd_music_dir + myTagEditor->CurrentDir() + "/" + old_dir;
			std::string full_new_dir = Config.mpd_music_dir + myTagEditor->CurrentDir() + "/" + new_dir;
			if (rename(full_old_dir.c_str(), full_new_dir.c_str()) == 0)
			{
				const char msg[] = "Directory renamed to \"%1%\"";
				Statusbar::printf(msg, wideShorten(new_dir, COLS-const_strlen(msg)));
				Mpd.UpdateDirectory(myTagEditor->CurrentDir());
			}
			else
			{
				const char msg[] = "Couldn't rename \"%1%\": %2%";
				Statusbar::printf(msg, wideShorten(old_dir, COLS-const_strlen(msg)-25), strerror(errno));
			}
		}
	}
#	endif // HAVE_TAGLIB_H
}

bool EditPlaylistName::canBeRun()
{
	return   (myScreen->isActiveWindow(myPlaylistEditor->Playlists)
	      && !myPlaylistEditor->Playlists.empty())
	    ||   (myScreen == myBrowser
	      && !myBrowser->main().empty()
		  && myBrowser->main().current()->value().type() == MPD::Item::Type::Playlist);
}

void EditPlaylistName::run()
{
	using Global::wFooter;
	std::string old_name, new_name;
	if (myScreen->isActiveWindow(myPlaylistEditor->Playlists))
		old_name = myPlaylistEditor->Playlists.current()->value().path();
	else
		old_name = myBrowser->main().current()->value().playlist().path();
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << NC::Format::Bold << "Playlist: " << NC::Format::NoBold;
		new_name = wFooter->prompt(old_name);
	}
	if (!new_name.empty() && new_name != old_name)
	{
		Mpd.Rename(old_name, new_name);
		const char msg[] = "Playlist renamed to \"%1%\"";
		Statusbar::printf(msg, wideShorten(new_name, COLS-const_strlen(msg)));
	}
}

bool EditLyrics::canBeRun()
{
	return myScreen == myLyrics;
}

void EditLyrics::run()
{
	myLyrics->Edit();
}

bool JumpToBrowser::canBeRun()
{
	m_song = currentSong(myScreen);
	return m_song != nullptr;
}

void JumpToBrowser::run()
{
	myBrowser->locateSong(*m_song);
}

bool JumpToMediaLibrary::canBeRun()
{
	m_song = currentSong(myScreen);
	return m_song != nullptr;
}

void JumpToMediaLibrary::run()
{
	myLibrary->LocateSong(*m_song);
}

bool JumpToPlaylistEditor::canBeRun()
{
	return myScreen == myBrowser
	    && myBrowser->main().current()->value().type() == MPD::Item::Type::Playlist;
}

void JumpToPlaylistEditor::run()
{
	myPlaylistEditor->Locate(myBrowser->main().current()->value().playlist());
}

void ToggleScreenLock::run()
{
	using Global::wFooter;
	using Global::myLockedScreen;
	const char *msg_unlockable_screen = "Current screen can't be locked";
	if (myLockedScreen != nullptr)
	{
		BaseScreen::unlock();
		Actions::setResizeFlags();
		myScreen->resize();
		Statusbar::print("Screen unlocked");
	}
	else if (!myScreen->isLockable())
	{
		Statusbar::print(msg_unlockable_screen);
	}
	else
	{
		unsigned part = Config.locked_screen_width_part*100;
		if (Config.ask_for_locked_screen_width_part)
		{
			Statusbar::ScopedLock slock;
			Statusbar::put() << "% of the locked screen's width to be reserved (20-80): ";
			part = fromString<unsigned>(wFooter->prompt(boost::lexical_cast<std::string>(part)));
		}
		boundsCheck(part, 20u, 80u);
		Config.locked_screen_width_part = part/100.0;
		if (myScreen->lock())
			Statusbar::printf("Screen locked (with %1%%% width)", part);
		else
			Statusbar::print(msg_unlockable_screen);
	}
}

bool JumpToTagEditor::canBeRun()
{
#	ifdef HAVE_TAGLIB_H
	m_song = currentSong(myScreen);
	return m_song != nullptr && isMPDMusicDirSet();
#	else
	return false;
#	endif // HAVE_TAGLIB_H
}

void JumpToTagEditor::run()
{
#	ifdef HAVE_TAGLIB_H
	myTagEditor->LocateSong(*m_song);
#	endif // HAVE_TAGLIB_H
}

bool JumpToPositionInSong::canBeRun()
{
	return Status::State::player() != MPD::psStop && Status::State::totalTime() > 0;
}

void JumpToPositionInSong::run()
{
	using Global::wFooter;
	
	const MPD::Song s = myPlaylist->nowPlayingSong();
	
	std::string spos;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Position to go (in %/m:ss/seconds(s)): ";
		spos = wFooter->prompt();
	}
	
	boost::regex rx;
	boost::smatch what;
	if (boost::regex_match(spos, what, rx.assign("([0-9]+):([0-9]{2})"))) // mm:ss
	{
		auto mins = fromString<unsigned>(what[1]);
		auto secs = fromString<unsigned>(what[2]);
		boundsCheck(secs, 0u, 60u);
		Mpd.Seek(s.getPosition(), mins * 60 + secs);
	}
	else if (boost::regex_match(spos, what, rx.assign("([0-9]+)s"))) // position in seconds
	{
		auto secs = fromString<unsigned>(what[1]);
		Mpd.Seek(s.getPosition(), secs);
	}
	else if (boost::regex_match(spos, what, rx.assign("([0-9]+)[%]{0,1}"))) // position in %
	{
		auto percent = fromString<unsigned>(what[1]);
		boundsCheck(percent, 0u, 100u);
		int secs = (percent * s.getDuration()) / 100.0;
		Mpd.Seek(s.getPosition(), secs);
	}
	else
		Statusbar::print("Invalid format ([m]:[ss], [s]s, [%]%, [%] accepted)");
}

bool SelectItem::canBeRun()
{
	m_list = dynamic_cast<NC::List *>(myScreen->activeWindow());
	return m_list != nullptr
	    && !m_list->empty()
	    && m_list->currentP()->isSelectable();
}

void SelectItem::run()
{
	auto current = m_list->currentP();
	current->setSelected(!current->isSelected());
	myScreen->scroll(NC::Scroll::Down);
	listsChangeFinisher();
}

bool ReverseSelection::canBeRun()
{
	m_list = dynamic_cast<NC::List *>(myScreen->activeWindow());
	return m_list != nullptr;
}

void ReverseSelection::run()
{
	for (auto &p : *m_list)
		p.setSelected(!p.isSelected());
	Statusbar::print("Selection reversed");
}

bool RemoveSelection::canBeRun()
{
	m_list = dynamic_cast<NC::List *>(myScreen->activeWindow());
	return m_list != nullptr;
}

void RemoveSelection::run()
{
	for (auto &p : *m_list)
		p.setSelected(false);
	Statusbar::print("Selection removed");
}

bool SelectAlbum::canBeRun()
{
	auto *w = myScreen->activeWindow();
	if (m_list != static_cast<void *>(w))
		m_list = dynamic_cast<NC::List *>(w);
	if (m_songs != static_cast<void *>(w))
		m_songs = dynamic_cast<SongList *>(w);
	return m_list != nullptr && !m_list->empty()
	    && m_songs != nullptr;
}

void SelectAlbum::run()
{
	const auto front = m_songs->beginS(), current = m_songs->currentS(), end = m_songs->endS();
	auto *s = current->get<Bit::Song>();
	if (s == nullptr)
		return;
	auto get = &MPD::Song::getAlbum;
	const std::string tag = s->getTags(get);
	// go up
	for (auto it = current; it != front;)
	{
		--it;
		s = it->get<Bit::Song>();
		if (s == nullptr || s->getTags(get) != tag)
			break;
		it->get<Bit::Properties>().setSelected(true);
	}
	// go down
	for (auto it = current;;)
	{
		it->get<Bit::Properties>().setSelected(true);
		if (++it == end)
			break;
		s = it->get<Bit::Song>();
		if (s == nullptr || s->getTags(get) != tag)
			break;
	}
	Statusbar::print("Album around cursor position selected");
}

bool AddSelectedItems::canBeRun()
{
	return myScreen != mySelectedItemsAdder;
}

void AddSelectedItems::run()
{
	mySelectedItemsAdder->switchTo();
}

void CropMainPlaylist::run()
{
	auto &w = myPlaylist->main();
	// cropping doesn't make sense in this case
	if (w.size() <= 1)
		return;
	if (Config.ask_before_clearing_playlists)
		confirmAction("Do you really want to crop main playlist?");
	Statusbar::print("Cropping playlist...");
	selectCurrentIfNoneSelected(w);
	cropPlaylist(w, std::bind(&MPD::Connection::Delete, ph::_1, ph::_2));
	Statusbar::print("Playlist cropped");
}

bool CropPlaylist::canBeRun()
{
	return myScreen == myPlaylistEditor;
}

void CropPlaylist::run()
{
	auto &w = myPlaylistEditor->Content;
	// cropping doesn't make sense in this case
	if (w.size() <= 1)
		return;
	assert(!myPlaylistEditor->Playlists.empty());
	std::string playlist = myPlaylistEditor->Playlists.current()->value().path();
	if (Config.ask_before_clearing_playlists)
		confirmAction(boost::format("Do you really want to crop playlist \"%1%\"?") % playlist);
	selectCurrentIfNoneSelected(w);
	Statusbar::printf("Cropping playlist \"%1%\"...", playlist);
	cropPlaylist(w, std::bind(&MPD::Connection::PlaylistDelete, ph::_1, playlist, ph::_2));
	Statusbar::printf("Playlist \"%1%\" cropped", playlist);
}

void ClearMainPlaylist::run()
{
	if (!myPlaylist->main().empty() && Config.ask_before_clearing_playlists)
		confirmAction("Do you really want to clear main playlist?");
	Mpd.ClearMainPlaylist();
	Statusbar::print("Playlist cleared");
	myPlaylist->main().reset();
}

bool ClearPlaylist::canBeRun()
{
	return myScreen == myPlaylistEditor;
}

void ClearPlaylist::run()
{
	if (myPlaylistEditor->Playlists.empty())
		return;
	std::string playlist = myPlaylistEditor->Playlists.current()->value().path();
	if (Config.ask_before_clearing_playlists)
		confirmAction(boost::format("Do you really want to clear playlist \"%1%\"?") % playlist);
	Mpd.ClearPlaylist(playlist);
	Statusbar::printf("Playlist \"%1%\" cleared", playlist);
}

bool SortPlaylist::canBeRun()
{
	return myScreen == myPlaylist;
}

void SortPlaylist::run()
{
	mySortPlaylistDialog->switchTo();
}

bool ReversePlaylist::canBeRun()
{
	return myScreen == myPlaylist;
}

void ReversePlaylist::run()
{
	myPlaylist->Reverse();
}

bool Find::canBeRun()
{
	return myScreen == myHelp
	    || myScreen == myLyrics
#	ifdef HAVE_CURL_CURL_H
	    || myScreen == myLastfm
#	endif // HAVE_CURL_CURL_H
	;
}

void Find::run()
{
	using Global::wFooter;
	
	std::string token;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Find: ";
		token = wFooter->prompt();
	}
	
	Statusbar::print("Searching...");
	auto s = static_cast<Screen<NC::Scrollpad> *>(myScreen);
	s->main().removeProperties();
	if (token.empty() || s->main().setProperties(NC::Format::Reverse, token, NC::Format::NoReverse, Config.regex_type))
		Statusbar::print("Done");
	else
		Statusbar::print("No matching patterns found");
	s->main().flush();
}

bool FindItemBackward::canBeRun()
{
	auto w = dynamic_cast<Searchable *>(myScreen);
	return w && w->allowsSearching();
}

void FindItemForward::run()
{
	findItem(SearchDirection::Forward);
	listsChangeFinisher();
}

bool FindItemForward::canBeRun()
{
	auto w = dynamic_cast<Searchable *>(myScreen);
	return w && w->allowsSearching();
}

void FindItemBackward::run()
{
	findItem(SearchDirection::Backward);
	listsChangeFinisher();
}

bool NextFoundItem::canBeRun()
{
	return dynamic_cast<Searchable *>(myScreen);
}

void NextFoundItem::run()
{
	Searchable *w = dynamic_cast<Searchable *>(myScreen);
	assert(w != nullptr);
	w->find(SearchDirection::Forward, Config.wrapped_search, true);
	listsChangeFinisher();
}

bool PreviousFoundItem::canBeRun()
{
	return dynamic_cast<Searchable *>(myScreen);
}

void PreviousFoundItem::run()
{
	Searchable *w = dynamic_cast<Searchable *>(myScreen);
	assert(w != nullptr);
	w->find(SearchDirection::Backward, Config.wrapped_search, true);
	listsChangeFinisher();
}

void ToggleFindMode::run()
{
	Config.wrapped_search = !Config.wrapped_search;
	Statusbar::printf("Search mode: %1%",
		Config.wrapped_search ? "Wrapped" : "Normal"
	);
}

void ToggleReplayGainMode::run()
{
	using Global::wFooter;
	
	char rgm = 0;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Replay gain mode? "
		<< "[" << NC::Format::Bold << 'o' << NC::Format::NoBold << "ff"
		<< "/" << NC::Format::Bold << 't' << NC::Format::NoBold << "rack"
		<< "/" << NC::Format::Bold << 'a' << NC::Format::NoBold << "lbum"
		<< "] ";
		rgm = Statusbar::Helpers::promptReturnOneOf({"t", "a", "o"})[0];
	}
	switch (rgm)
	{
		case 't':
			Mpd.SetReplayGainMode(MPD::rgmTrack);
			break;
		case 'a':
			Mpd.SetReplayGainMode(MPD::rgmAlbum);
			break;
		case 'o':
			Mpd.SetReplayGainMode(MPD::rgmOff);
			break;
		default: // impossible
			throw std::runtime_error(
				(boost::format("ToggleReplayGainMode: impossible case reached: %1%") % rgm).str()
			);
	}
	Statusbar::printf("Replay gain mode: %1%", Mpd.GetReplayGainMode());
}

void ToggleAddMode::run()
{
	std::string mode_desc;
	switch (Config.space_add_mode)
	{
		case SpaceAddMode::AddRemove:
			Config.space_add_mode = SpaceAddMode::AlwaysAdd;
			mode_desc = "always add an item to playlist";
			break;
		case SpaceAddMode::AlwaysAdd:
			Config.space_add_mode = SpaceAddMode::AddRemove;
			mode_desc = "add an item to playlist or remove if already added";
			break;
	}
	Statusbar::printf("Add mode: %1%", mode_desc);
}

void ToggleMouse::run()
{
	Config.mouse_support = !Config.mouse_support;
	if (Config.mouse_support)
		NC::Mouse::enable();
	else
		NC::Mouse::disable();
	Statusbar::printf("Mouse support %1%",
		Config.mouse_support ? "enabled" : "disabled"
	);
}

void ToggleBitrateVisibility::run()
{
	Config.display_bitrate = !Config.display_bitrate;
	Statusbar::printf("Bitrate visibility %1%",
		Config.display_bitrate ? "enabled" : "disabled"
	);
}

void AddRandomItems::run()
{
	using Global::wFooter;
	char rnd_type = 0;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Add random? "
		<< "[" << NC::Format::Bold << 's' << NC::Format::NoBold << "ongs"
		<< "/" << NC::Format::Bold << 'a' << NC::Format::NoBold << "rtists"
		<< "/" << "album" << NC::Format::Bold << 'A' << NC::Format::NoBold << "rtists"
		<< "/" << "al" << NC::Format::Bold << 'b' << NC::Format::NoBold << "ums"
		<< "] ";
		rnd_type = Statusbar::Helpers::promptReturnOneOf({"s", "a", "A", "b"})[0];
	}

	mpd_tag_type tag_type = MPD_TAG_ARTIST;
	std::string tag_type_str ;
	if (rnd_type != 's')
	{
		tag_type = charToTagType(rnd_type);
		tag_type_str = boost::locale::to_lower(tagTypeToString(tag_type));
	}
	else
		tag_type_str = "song";
	
	unsigned number;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Number of random " << tag_type_str << "s: ";
		number = fromString<unsigned>(wFooter->prompt());
	}
	if (number && (rnd_type == 's' ? Mpd.AddRandomSongs(number) : Mpd.AddRandomTag(tag_type, number)))
	{
		Statusbar::printf("%1% random %2%%3% added to playlist",
			number, tag_type_str, number == 1 ? "" : "s"
		);
	}
}

bool ToggleBrowserSortMode::canBeRun()
{
	return myScreen == myBrowser;
}

void ToggleBrowserSortMode::run()
{
	switch (Config.browser_sort_mode)
	{
		case SortMode::Name:
			Config.browser_sort_mode = SortMode::ModificationTime;
			Statusbar::print("Sort songs by: modification time");
			break;
		case SortMode::ModificationTime:
			Config.browser_sort_mode = SortMode::CustomFormat;
			Statusbar::print("Sort songs by: custom format");
			break;
		case SortMode::CustomFormat:
			Config.browser_sort_mode = SortMode::NoOp;
			Statusbar::print("Do not sort songs");
			break;
		case SortMode::NoOp:
			Config.browser_sort_mode = SortMode::Name;
			Statusbar::print("Sort songs by: name");
	}
	if (Config.browser_sort_mode != SortMode::NoOp)
	{
		size_t sort_offset = myBrowser->inRootDirectory() ? 0 : 1;
		std::sort(myBrowser->main().begin()+sort_offset, myBrowser->main().end(),
			LocaleBasedItemSorting(std::locale(), Config.ignore_leading_the, Config.browser_sort_mode)
		);
	}
}

bool ToggleLibraryTagType::canBeRun()
{
	return (myScreen->isActiveWindow(myLibrary->Tags))
	    || (myLibrary->Columns() == 2 && myScreen->isActiveWindow(myLibrary->Albums));
}

void ToggleLibraryTagType::run()
{
	using Global::wFooter;
	
	char tag_type = 0;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Tag type? "
		<< "[" << NC::Format::Bold << 'a' << NC::Format::NoBold << "rtist"
		<< "/" << "album" << NC::Format::Bold << 'A' << NC::Format::NoBold << "rtist"
		<< "/" << NC::Format::Bold << 'y' << NC::Format::NoBold << "ear"
		<< "/" << NC::Format::Bold << 'g' << NC::Format::NoBold << "enre"
		<< "/" << NC::Format::Bold << 'c' << NC::Format::NoBold << "omposer"
		<< "/" << NC::Format::Bold << 'p' << NC::Format::NoBold << "erformer"
		<< "] ";
		tag_type = Statusbar::Helpers::promptReturnOneOf({"a", "A", "y", "g", "c", "p"})[0];
	}
	mpd_tag_type new_tagitem = charToTagType(tag_type);
	if (new_tagitem != Config.media_lib_primary_tag)
	{
		Config.media_lib_primary_tag = new_tagitem;
		std::string item_type = tagTypeToString(Config.media_lib_primary_tag);
		myLibrary->Tags.setTitle(Config.titles_visibility ? item_type + "s" : "");
		myLibrary->Tags.reset();
		item_type = boost::locale::to_lower(item_type);
		std::string and_mtime = Config.media_library_sort_by_mtime ?
		                        " and mtime" :
		                        "";
		if (myLibrary->Columns() == 2)
		{
			myLibrary->Songs.clear();
			myLibrary->Albums.reset();
			myLibrary->Albums.clear();
			myLibrary->Albums.setTitle(Config.titles_visibility ? "Albums (sorted by " + item_type + and_mtime + ")" : "");
			myLibrary->Albums.display();
		}
		else
		{
			myLibrary->Tags.clear();
			myLibrary->Tags.display();
		}
		Statusbar::printf("Switched to the list of %1%s", item_type);
	}
}

bool ToggleMediaLibrarySortMode::canBeRun()
{
	return myScreen == myLibrary;
}

void ToggleMediaLibrarySortMode::run()
{
	myLibrary->toggleSortMode();
}

bool RefetchLyrics::canBeRun()
{
#	ifdef HAVE_CURL_CURL_H
	return myScreen == myLyrics;
#	else
	return false;
#	endif // HAVE_CURL_CURL_H
}

void RefetchLyrics::run()
{
#	ifdef HAVE_CURL_CURL_H
	myLyrics->Refetch();
#	endif // HAVE_CURL_CURL_H
}

bool SetSelectedItemsPriority::canBeRun()
{
	if (Mpd.Version() < 17)
	{
		Statusbar::print("Priorities are supported in MPD >= 0.17.0");
		return false;
	}
	return myScreen == myPlaylist && !myPlaylist->main().empty();
}

void SetSelectedItemsPriority::run()
{
	using Global::wFooter;
	
	unsigned prio;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Set priority [0-255]: ";
		prio = fromString<unsigned>(wFooter->prompt());
		boundsCheck(prio, 0u, 255u);
	}
	myPlaylist->SetSelectedItemsPriority(prio);
}

bool ToggleVisualizationType::canBeRun()
{
#	ifdef ENABLE_VISUALIZER
	return myScreen == myVisualizer;
#	else
	return false;
#	endif // ENABLE_VISUALIZER
}

void ToggleVisualizationType::run()
{
#	ifdef ENABLE_VISUALIZER
	myVisualizer->ToggleVisualizationType();
#	endif // ENABLE_VISUALIZER
}

bool SetVisualizerSampleMultiplier::canBeRun()
{
#	ifdef ENABLE_VISUALIZER
	return myScreen == myVisualizer;
#	else
	return false;
#	endif // ENABLE_VISUALIZER
}

void SetVisualizerSampleMultiplier::run()
{
#	ifdef ENABLE_VISUALIZER
	using Global::wFooter;

	double multiplier;
	{
		Statusbar::ScopedLock slock;
		Statusbar::put() << "Set visualizer sample multiplier: ";
		multiplier = fromString<double>(wFooter->prompt());
		lowerBoundCheck(multiplier, 0.0);
		Config.visualizer_sample_multiplier = multiplier;
	}
	Statusbar::printf("Visualizer sample multiplier set to %1%", multiplier);
#	endif // ENABLE_VISUALIZER
}

void ShowSongInfo::run()
{
	mySongInfo->switchTo();
}

bool ShowArtistInfo::canBeRun()
{
	#ifdef HAVE_CURL_CURL_H
	return myScreen == myLastfm
	  ||   (myScreen->isActiveWindow(myLibrary->Tags)
	    && !myLibrary->Tags.empty()
	    && Config.media_lib_primary_tag == MPD_TAG_ARTIST)
	  ||   currentSong(myScreen);
#	else
	return false;
#	endif // NOT HAVE_CURL_CURL_H
}

void ShowArtistInfo::run()
{
#	ifdef HAVE_CURL_CURL_H
	if (myScreen == myLastfm)
	{
		myLastfm->switchTo();
		return;
	}
	
	std::string artist;
	if (myScreen->isActiveWindow(myLibrary->Tags))
	{
		assert(!myLibrary->Tags.empty());
		assert(Config.media_lib_primary_tag == MPD_TAG_ARTIST);
		artist = myLibrary->Tags.current()->value().tag();
	}
	else
	{
		auto s = currentSong(myScreen);
		assert(s);
		artist = s->getArtist();
	}
	
	if (!artist.empty())
	{
		myLastfm->queueJob(LastFm::ArtistInfo(artist, Config.lastfm_preferred_language));
		myLastfm->switchTo();
	}
#	endif // HAVE_CURL_CURL_H
}

void ShowLyrics::run()
{
	myLyrics->switchTo();
}

void Quit::run()
{
	ExitMainLoop = true;
}

void NextScreen::run()
{
	if (Config.screen_switcher_previous)
	{
		if (auto tababble = dynamic_cast<Tabbable *>(myScreen))
			tababble->switchToPreviousScreen();
	}
	else if (!Config.screen_sequence.empty())
	{
		const auto &seq = Config.screen_sequence;
		auto screen_type = std::find(seq.begin(), seq.end(), myScreen->type());
		if (++screen_type == seq.end())
			toScreen(seq.front())->switchTo();
		else
			toScreen(*screen_type)->switchTo();
	}
}

void PreviousScreen::run()
{
	if (Config.screen_switcher_previous)
	{
		if (auto tababble = dynamic_cast<Tabbable *>(myScreen))
			tababble->switchToPreviousScreen();
	}
	else if (!Config.screen_sequence.empty())
	{
		const auto &seq = Config.screen_sequence;
		auto screen_type = std::find(seq.begin(), seq.end(), myScreen->type());
		if (screen_type == seq.begin())
			toScreen(seq.back())->switchTo();
		else
			toScreen(*--screen_type)->switchTo();
	}
}

bool ShowHelp::canBeRun()
{
	return myScreen != myHelp
#	ifdef HAVE_TAGLIB_H
	    && myScreen != myTinyTagEditor
#	endif // HAVE_TAGLIB_H
	;
}

void ShowHelp::run()
{
	myHelp->switchTo();
}

bool ShowPlaylist::canBeRun()
{
	return myScreen != myPlaylist
#	ifdef HAVE_TAGLIB_H
	    && myScreen != myTinyTagEditor
#	endif // HAVE_TAGLIB_H
	;
}

void ShowPlaylist::run()
{
	myPlaylist->switchTo();
}

bool ShowBrowser::canBeRun()
{
	return myScreen != myBrowser
#	ifdef HAVE_TAGLIB_H
	    && myScreen != myTinyTagEditor
#	endif // HAVE_TAGLIB_H
	;
}

void ShowBrowser::run()
{
	myBrowser->switchTo();
}

bool ChangeBrowseMode::canBeRun()
{
	return myScreen == myBrowser;
}

void ChangeBrowseMode::run()
{
	myBrowser->changeBrowseMode();
}

bool ShowSearchEngine::canBeRun()
{
	return myScreen != mySearcher
#	ifdef HAVE_TAGLIB_H
	    && myScreen != myTinyTagEditor
#	endif // HAVE_TAGLIB_H
	;
}

void ShowSearchEngine::run()
{
	mySearcher->switchTo();
}

bool ResetSearchEngine::canBeRun()
{
	return myScreen == mySearcher;
}

void ResetSearchEngine::run()
{
	mySearcher->reset();
}

bool ShowMediaLibrary::canBeRun()
{
	return myScreen != myLibrary
#	ifdef HAVE_TAGLIB_H
	    && myScreen != myTinyTagEditor
#	endif // HAVE_TAGLIB_H
	;
}

void ShowMediaLibrary::run()
{
	myLibrary->switchTo();
}

bool ToggleMediaLibraryColumnsMode::canBeRun()
{
	return myScreen == myLibrary;
}

void ToggleMediaLibraryColumnsMode::run()
{
	myLibrary->toggleColumnsMode();
	myLibrary->refresh();
}

bool ShowPlaylistEditor::canBeRun()
{
	return myScreen != myPlaylistEditor
#	ifdef HAVE_TAGLIB_H
	    && myScreen != myTinyTagEditor
#	endif // HAVE_TAGLIB_H
	;
}

void ShowPlaylistEditor::run()
{
	myPlaylistEditor->switchTo();
}

bool ShowTagEditor::canBeRun()
{
#	ifdef HAVE_TAGLIB_H
	return myScreen != myTagEditor
	    && myScreen != myTinyTagEditor;
#	else
	return false;
#	endif // HAVE_TAGLIB_H
}

void ShowTagEditor::run()
{
#	ifdef HAVE_TAGLIB_H
	if (isMPDMusicDirSet())
		myTagEditor->switchTo();
#	endif // HAVE_TAGLIB_H
}

bool ShowOutputs::canBeRun()
{
#	ifdef ENABLE_OUTPUTS
	return myScreen != myOutputs
#	ifdef HAVE_TAGLIB_H	
	    && myScreen != myTinyTagEditor
#	endif // HAVE_TAGLIB_H
	;
#	else
	return false;
#	endif // ENABLE_OUTPUTS
}

void ShowOutputs::run()
{
#	ifdef ENABLE_OUTPUTS
	myOutputs->switchTo();
#	endif // ENABLE_OUTPUTS
}

bool ShowVisualizer::canBeRun()
{
#	ifdef ENABLE_VISUALIZER
	return myScreen != myVisualizer
#	ifdef HAVE_TAGLIB_H
	    && myScreen != myTinyTagEditor
#	endif // HAVE_TAGLIB_H
	;
#	else
	return false;
#	endif // ENABLE_VISUALIZER
}

void ShowVisualizer::run()
{
#	ifdef ENABLE_VISUALIZER
	myVisualizer->switchTo();
#	endif // ENABLE_VISUALIZER
}

bool ShowClock::canBeRun()
{
#	ifdef ENABLE_CLOCK
	return myScreen != myClock
#	ifdef HAVE_TAGLIB_H
	    && myScreen != myTinyTagEditor
#	endif // HAVE_TAGLIB_H
	;
#	else
	return false;
#	endif // ENABLE_CLOCK
}

void ShowClock::run()
{
#	ifdef ENABLE_CLOCK
	myClock->switchTo();
#	endif // ENABLE_CLOCK
}

#ifdef HAVE_TAGLIB_H
bool ShowServerInfo::canBeRun()
{
	return myScreen != myTinyTagEditor;
}
#endif // HAVE_TAGLIB_H

void ShowServerInfo::run()
{
	myServerInfo->switchTo();
}

}

namespace {

void populateActions()
{
	auto insert_action = [](Actions::BaseAction *a) {
		AvailableActions[static_cast<size_t>(a->type())] = a;
	};
	insert_action(new Actions::Dummy());
	insert_action(new Actions::UpdateEnvironment());
	insert_action(new Actions::MouseEvent());
	insert_action(new Actions::ScrollUp());
	insert_action(new Actions::ScrollDown());
	insert_action(new Actions::ScrollUpArtist());
	insert_action(new Actions::ScrollUpAlbum());
	insert_action(new Actions::ScrollDownArtist());
	insert_action(new Actions::ScrollDownAlbum());
	insert_action(new Actions::PageUp());
	insert_action(new Actions::PageDown());
	insert_action(new Actions::MoveHome());
	insert_action(new Actions::MoveEnd());
	insert_action(new Actions::ToggleInterface());
	insert_action(new Actions::JumpToParentDirectory());
	insert_action(new Actions::PressEnter());
	insert_action(new Actions::SelectItem());
	insert_action(new Actions::PreviousColumn());
	insert_action(new Actions::NextColumn());
	insert_action(new Actions::MasterScreen());
	insert_action(new Actions::SlaveScreen());
	insert_action(new Actions::VolumeUp());
	insert_action(new Actions::VolumeDown());
	insert_action(new Actions::AddItemToPlaylist());
	insert_action(new Actions::DeletePlaylistItems());
	insert_action(new Actions::DeleteStoredPlaylist());
	insert_action(new Actions::DeleteBrowserItems());
	insert_action(new Actions::ReplaySong());
	insert_action(new Actions::PreviousSong());
	insert_action(new Actions::NextSong());
	insert_action(new Actions::Pause());
	insert_action(new Actions::Stop());
	insert_action(new Actions::ExecuteCommand());
	insert_action(new Actions::SavePlaylist());
	insert_action(new Actions::MoveSortOrderUp());
	insert_action(new Actions::MoveSortOrderDown());
	insert_action(new Actions::MoveSelectedItemsUp());
	insert_action(new Actions::MoveSelectedItemsDown());
	insert_action(new Actions::MoveSelectedItemsTo());
	insert_action(new Actions::Add());
	insert_action(new Actions::SeekForward());
	insert_action(new Actions::SeekBackward());
	insert_action(new Actions::ToggleDisplayMode());
	insert_action(new Actions::ToggleSeparatorsBetweenAlbums());
	insert_action(new Actions::ToggleLyricsUpdateOnSongChange());
	insert_action(new Actions::ToggleLyricsFetcher());
	insert_action(new Actions::ToggleFetchingLyricsInBackground());
	insert_action(new Actions::TogglePlayingSongCentering());
	insert_action(new Actions::UpdateDatabase());
	insert_action(new Actions::JumpToPlayingSong());
	insert_action(new Actions::ToggleRepeat());
	insert_action(new Actions::Shuffle());
	insert_action(new Actions::ToggleRandom());
	insert_action(new Actions::StartSearching());
	insert_action(new Actions::SaveTagChanges());
	insert_action(new Actions::ToggleSingle());
	insert_action(new Actions::ToggleConsume());
	insert_action(new Actions::ToggleCrossfade());
	insert_action(new Actions::SetCrossfade());
	insert_action(new Actions::SetVolume());
	insert_action(new Actions::EditSong());
	insert_action(new Actions::EditLibraryTag());
	insert_action(new Actions::EditLibraryAlbum());
	insert_action(new Actions::EditDirectoryName());
	insert_action(new Actions::EditPlaylistName());
	insert_action(new Actions::EditLyrics());
	insert_action(new Actions::JumpToBrowser());
	insert_action(new Actions::JumpToMediaLibrary());
	insert_action(new Actions::JumpToPlaylistEditor());
	insert_action(new Actions::ToggleScreenLock());
	insert_action(new Actions::JumpToTagEditor());
	insert_action(new Actions::JumpToPositionInSong());
	insert_action(new Actions::ReverseSelection());
	insert_action(new Actions::RemoveSelection());
	insert_action(new Actions::SelectAlbum());
	insert_action(new Actions::AddSelectedItems());
	insert_action(new Actions::CropMainPlaylist());
	insert_action(new Actions::CropPlaylist());
	insert_action(new Actions::ClearMainPlaylist());
	insert_action(new Actions::ClearPlaylist());
	insert_action(new Actions::SortPlaylist());
	insert_action(new Actions::ReversePlaylist());
	insert_action(new Actions::Find());
	insert_action(new Actions::FindItemForward());
	insert_action(new Actions::FindItemBackward());
	insert_action(new Actions::NextFoundItem());
	insert_action(new Actions::PreviousFoundItem());
	insert_action(new Actions::ToggleFindMode());
	insert_action(new Actions::ToggleReplayGainMode());
	insert_action(new Actions::ToggleAddMode());
	insert_action(new Actions::ToggleMouse());
	insert_action(new Actions::ToggleBitrateVisibility());
	insert_action(new Actions::AddRandomItems());
	insert_action(new Actions::ToggleBrowserSortMode());
	insert_action(new Actions::ToggleLibraryTagType());
	insert_action(new Actions::ToggleMediaLibrarySortMode());
	insert_action(new Actions::RefetchLyrics());
	insert_action(new Actions::SetSelectedItemsPriority());
	insert_action(new Actions::ToggleVisualizationType());
	insert_action(new Actions::SetVisualizerSampleMultiplier());
	insert_action(new Actions::ShowSongInfo());
	insert_action(new Actions::ShowArtistInfo());
	insert_action(new Actions::ShowLyrics());
	insert_action(new Actions::Quit());
	insert_action(new Actions::NextScreen());
	insert_action(new Actions::PreviousScreen());
	insert_action(new Actions::ShowHelp());
	insert_action(new Actions::ShowPlaylist());
	insert_action(new Actions::ShowBrowser());
	insert_action(new Actions::ChangeBrowseMode());
	insert_action(new Actions::ShowSearchEngine());
	insert_action(new Actions::ResetSearchEngine());
	insert_action(new Actions::ShowMediaLibrary());
	insert_action(new Actions::ToggleMediaLibraryColumnsMode());
	insert_action(new Actions::ShowPlaylistEditor());
	insert_action(new Actions::ShowTagEditor());
	insert_action(new Actions::ShowOutputs());
	insert_action(new Actions::ShowVisualizer());
	insert_action(new Actions::ShowClock());
	insert_action(new Actions::ShowServerInfo());
}

bool scrollTagCanBeRun(NC::List *&list, SongList *&songs)
{
	auto w = myScreen->activeWindow();
	if (list != static_cast<void *>(w))
		list = dynamic_cast<NC::List *>(w);
	if (songs != static_cast<void *>(w))
		songs = dynamic_cast<SongList *>(w);
	return list != nullptr && !list->empty()
	    && songs != nullptr;
}

void scrollTagUpRun(NC::List *list, SongList *songs, MPD::Song::GetFunction get)
{
	const auto front = songs->beginS();
	auto it = songs->currentS();
	if (auto *s = it->get<Bit::Song>())
	{
		const std::string tag = s->getTags(get);
		while (it != front)
		{
			--it;
			s = it->get<Bit::Song>();
			if (s == nullptr || s->getTags(get) != tag)
				break;
		}
		list->highlight(it-front);
	}
}

void scrollTagDownRun(NC::List *list, SongList *songs, MPD::Song::GetFunction get)
{
	const auto front = songs->beginS(), back = --songs->endS();
	auto it = songs->currentS();
	if (auto *s = it->get<Bit::Song>())
	{
		const std::string tag = s->getTags(get);
		while (it != back)
		{
			++it;
			s = it->get<Bit::Song>();
			if (s == nullptr || s->getTags(get) != tag)
				break;
		}
		list->highlight(it-front);
	}
}

void seek()
{
	using Global::wHeader;
	using Global::wFooter;
	using Global::Timer;
	using Global::SeekingInProgress;
	
	if (!Status::State::totalTime())
	{
		Statusbar::print("Unknown item length");
		return;
	}
	
	Progressbar::ScopedLock progressbar_lock;
	Statusbar::ScopedLock statusbar_lock;

	unsigned songpos = Status::State::elapsedTime();
	auto t = Timer;
	
	int old_timeout = wFooter->getTimeout();
	wFooter->setTimeout(BaseScreen::defaultWindowTimeout);
	
	auto seekForward = &Actions::get(Actions::Type::SeekForward);
	auto seekBackward = &Actions::get(Actions::Type::SeekBackward);
	
	SeekingInProgress = true;
	while (true)
	{
		Status::trace();
		
		unsigned howmuch = Config.incremental_seeking
		                 ? (Timer-t).total_seconds()/2+Config.seek_time
		                 : Config.seek_time;
		
		NC::Key::Type input = readKey(*wFooter);
		auto k = Bindings.get(input);
		if (k.first == k.second || !k.first->isSingle()) // no single action?
			break;
		auto a = k.first->action();
		if (a == seekForward)
		{
			if (songpos < Status::State::totalTime())
				songpos = std::min(songpos + howmuch, Status::State::totalTime());
		}
		else if (a == seekBackward)
		{
			if (songpos > 0)
			{
				if (songpos < howmuch)
					songpos = 0;
				else
					songpos -= howmuch;
			}
		}
		else
			break;
		
		*wFooter << NC::Format::Bold;
		std::string tracklength;
		// FIXME: merge this with the code in status.cpp
		switch (Config.design)
		{
			case Design::Classic:
				tracklength = " [";
				if (Config.display_remaining_time)
				{
					tracklength += "-";
					tracklength += MPD::Song::ShowTime(Status::State::totalTime()-songpos);
				}
				else
					tracklength += MPD::Song::ShowTime(songpos);
				tracklength += "/";
				tracklength += MPD::Song::ShowTime(Status::State::totalTime());
				tracklength += "]";
				*wFooter << NC::XY(wFooter->getWidth()-tracklength.length(), 1) << tracklength;
				break;
			case Design::Alternative:
				if (Config.display_remaining_time)
				{
					tracklength = "-";
					tracklength += MPD::Song::ShowTime(Status::State::totalTime()-songpos);
				}
				else
					tracklength = MPD::Song::ShowTime(songpos);
				tracklength += "/";
				tracklength += MPD::Song::ShowTime(Status::State::totalTime());
				*wHeader << NC::XY(0, 0) << tracklength << " ";
				wHeader->refresh();
				break;
		}
		*wFooter << NC::Format::NoBold;
		Progressbar::draw(songpos, Status::State::totalTime());
		wFooter->refresh();
	}
	SeekingInProgress = false;
	Mpd.Seek(Status::State::currentSongPosition(), songpos);
	
	wFooter->setTimeout(old_timeout);
}

void findItem(const SearchDirection direction)
{
	using Global::wFooter;
	
	Searchable *w = dynamic_cast<Searchable *>(myScreen);
	assert(w != nullptr);
	assert(w->allowsSearching());
	
	std::string constraint;
	{
		Statusbar::ScopedLock slock;
		NC::Window::ScopedPromptHook prompt_hook(*wFooter,
			Statusbar::Helpers::FindImmediately(w, direction)
		);
		Statusbar::put() << (boost::format("Find %1%: ") % direction).str();
		constraint = wFooter->prompt();
	}
	
	try
	{
		if (constraint.empty())
		{
			Statusbar::printf("Constraint unset");
			w->clearConstraint();
		}
		else
		{
			w->setSearchConstraint(constraint);
			Statusbar::printf("Using constraint \"%1%\"", constraint);
		}
	}
	catch (boost::bad_expression &e)
	{
		Statusbar::printf("%1%", e.what());
	}
}

void listsChangeFinisher()
{
	if (myScreen == myLibrary
	||  myScreen == myPlaylistEditor
#	ifdef HAVE_TAGLIB_H
	||  myScreen == myTagEditor
#	endif // HAVE_TAGLIB_H
	   )
	{
		if (myScreen->activeWindow() == &myLibrary->Tags)
		{
			myLibrary->Albums.clear();
			myLibrary->Albums.refresh();
			myLibrary->Songs.clear();
			myLibrary->Songs.refresh();
			myLibrary->updateTimer();
		}
		else if (myScreen->activeWindow() == &myLibrary->Albums)
		{
			myLibrary->Songs.clear();
			myLibrary->Songs.refresh();
			myLibrary->updateTimer();
		}
		else if (myScreen->isActiveWindow(myPlaylistEditor->Playlists))
		{
			myPlaylistEditor->Content.clear();
			myPlaylistEditor->Content.refresh();
			myPlaylistEditor->updateTimer();
		}
#		ifdef HAVE_TAGLIB_H
		else if (myScreen->activeWindow() == myTagEditor->Dirs)
		{
			myTagEditor->Tags->clear();
		}
#		endif // HAVE_TAGLIB_H
	}
}

}
