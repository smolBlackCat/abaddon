#include <gtkmm.h>
#include <memory>
#include <string>
#include <algorithm>
#include "discord/discord.hpp"
#include "dialogs/token.hpp"
#include "abaddon.hpp"

#ifdef _WIN32
    #pragma comment(lib, "crypt32.lib")
#endif

Abaddon::Abaddon()
    : m_settings("abaddon.ini") {
    m_discord.SetAbaddon(this);
    LoadFromSettings();
}

Abaddon::~Abaddon() {
    m_settings.Close();
    m_discord.Stop();
}

int Abaddon::StartGTK() {
    m_gtk_app = Gtk::Application::create("com.github.lorpus.abaddon");

    m_main_window = std::make_unique<MainWindow>();
    m_main_window->SetAbaddon(this);
    m_main_window->set_title("Abaddon");
    m_main_window->show();
    m_main_window->UpdateComponents();

    m_gtk_app->signal_shutdown().connect([&]() {
        StopDiscord();
    });

    if (!m_settings.IsValid()) {
        Gtk::MessageDialog dlg(*m_main_window, "The settings file could not be created!", false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        dlg.run();
    }

    return m_gtk_app->run(*m_main_window);
}

void Abaddon::LoadFromSettings() {
    std::string token = m_settings.GetSetting("discord", "token");
    if (token.size()) {
        m_discord_token = token;
        m_discord.UpdateToken(m_discord_token);
    }
}

void Abaddon::StartDiscord() {
    m_discord.Start();
}

void Abaddon::StopDiscord() {
    m_discord.Stop();
}

bool Abaddon::IsDiscordActive() const {
    return m_discord.IsStarted();
}

std::string Abaddon::GetDiscordToken() const {
    return m_discord_token;
}

const DiscordClient &Abaddon::GetDiscordClient() const {
    std::scoped_lock<std::mutex> guard(m_mutex);
    return m_discord;
}

void Abaddon::DiscordNotifyReady() {
    m_main_window->UpdateComponents();
}

void Abaddon::DiscordNotifyChannelListFullRefresh() {
    m_main_window->UpdateChannelListing();
}

void Abaddon::DiscordNotifyMessageCreate(Snowflake id) {
    m_main_window->UpdateChatNewMessage(id);
}

void Abaddon::ActionConnect() {
    if (!m_discord.IsStarted())
        StartDiscord();
    m_main_window->UpdateComponents();
}

void Abaddon::ActionDisconnect() {
    if (m_discord.IsStarted())
        StopDiscord();
    m_main_window->UpdateComponents();
}

void Abaddon::ActionSetToken() {
    TokenDialog dlg(*m_main_window);
    auto response = dlg.run();
    if (response == Gtk::RESPONSE_OK) {
        m_discord_token = dlg.GetToken();
        m_discord.UpdateToken(m_discord_token);
        m_main_window->UpdateComponents();
        m_settings.SetSetting("discord", "token", m_discord_token);
    }
}

void Abaddon::ActionMoveGuildUp(Snowflake id) {
    UserSettingsData d = m_discord.GetUserSettings();
    std::vector<Snowflake> &pos = d.GuildPositions;
    if (pos.size() == 0) {
        auto x = m_discord.GetUserSortedGuilds();
        for (const auto &pair : x)
            pos.push_back(pair.first);
    }

    auto it = std::find(pos.begin(), pos.end(), id);
    assert(it != pos.end());
    std::vector<Snowflake>::iterator left = it - 1;
    std::swap(*left, *it);

    m_discord.UpdateSettingsGuildPositions(pos);
}

void Abaddon::ActionMoveGuildDown(Snowflake id) {
    UserSettingsData d = m_discord.GetUserSettings();
    std::vector<Snowflake> &pos = d.GuildPositions;
    if (pos.size() == 0) {
        auto x = m_discord.GetUserSortedGuilds();
        for (const auto &pair : x)
            pos.push_back(pair.first);
    }

    auto it = std::find(pos.begin(), pos.end(), id);
    assert(it != pos.end());
    std::vector<Snowflake>::iterator right = it + 1;
    std::swap(*right, *it);

    m_discord.UpdateSettingsGuildPositions(pos);
}

void Abaddon::ActionListChannelItemClick(Snowflake id) {
    m_main_window->UpdateChatActiveChannel(id);
    if (m_channels_requested.find(id) == m_channels_requested.end()) {
        m_discord.FetchMessagesInChannel(id, [this, id](const std::vector<MessageData> &msgs) {
            m_channels_requested.insert(id);
            m_main_window->UpdateChatWindowContents();
        });
    } else {
        m_main_window->UpdateChatWindowContents();
    }
}

void Abaddon::ActionChatInputSubmit(std::string msg, Snowflake channel) {
    m_discord.SendChatMessage(msg, channel);
}

int main(int argc, char **argv) {
    Gtk::Main::init_gtkmm_internals(); // why???
    Abaddon abaddon;
    return abaddon.StartGTK();
}