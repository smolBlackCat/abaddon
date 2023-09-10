#include "memberlist.hpp"

MemberList::MemberList()
    : m_model(Gtk::TreeStore::create(m_columns)) {
    m_main.get_style_context()->add_class("member-list");

    m_view.set_hexpand(true);
    m_view.set_vexpand(true);

    m_view.set_show_expanders(false);
    m_view.set_enable_search(false);
    m_view.set_headers_visible(false);
    m_view.get_selection()->set_mode(Gtk::SELECTION_NONE);
    m_view.set_model(m_model);

    m_main.add(m_view);
    m_main.show_all_children();

    auto *column = Gtk::make_managed<Gtk::TreeView::Column>("display");
    auto *renderer = Gtk::make_managed<CellRendererMemberList>();
    column->pack_start(*renderer);
    column->add_attribute(renderer->property_type(), m_columns.m_type);
    column->add_attribute(renderer->property_id(), m_columns.m_id);
    column->add_attribute(renderer->property_name(), m_columns.m_name);
    column->add_attribute(renderer->property_pixbuf(), m_columns.m_pixbuf);
    column->add_attribute(renderer->property_color(), m_columns.m_color);
    m_view.append_column(*column);

    renderer->signal_render().connect(sigc::mem_fun(*this, &MemberList::OnCellRender));
}

Gtk::Widget *MemberList::GetRoot() {
    return &m_main;
}

void MemberList::UpdateMemberList() {
    Clear();
    if (!m_active_channel.IsValid()) return;

    auto &discord = Abaddon::Get().GetDiscordClient();

    const auto channel = discord.GetChannel(m_active_channel);
    if (!channel.has_value()) {
        spdlog::get("ui")->warn("attempted to update member list with unfetchable channel");
        return;
    }

    if (channel->IsDM()) {
        for (const auto &user : channel->GetDMRecipients()) {
            auto row = *m_model->append();
            row[m_columns.m_type] = MemberListRenderType::Member;
            row[m_columns.m_id] = user.ID;
            row[m_columns.m_name] = user.GetDisplayNameEscaped();
        }
    }

    const auto guild = discord.GetGuild(m_active_guild);
    if (!guild.has_value()) return;

    std::set<Snowflake> ids;
    if (channel->IsThread()) {
        const auto x = discord.GetUsersInThread(m_active_channel);
        ids = { x.begin(), x.end() };
    } else {
        ids = discord.GetUsersInGuild(m_active_guild);
    }

    std::map<int, RoleData> pos_to_role;
    std::map<int, std::vector<UserData>> pos_to_users;
    std::unordered_map<Snowflake, int> user_to_color;
    std::vector<Snowflake> roleless_users;

    for (const auto user_id : ids) {
        auto user = discord.GetUser(user_id);
        if (!user.has_value() || user->IsDeleted()) continue;

        const auto pos_role_id = discord.GetMemberHoistedRole(m_active_guild, user_id);
        const auto col_role_id = discord.GetMemberHoistedRole(m_active_guild, user_id, true);
        const auto pos_role = discord.GetRole(pos_role_id);
        const auto col_role = discord.GetRole(col_role_id);

        if (!pos_role.has_value()) {
            roleless_users.push_back(user_id);
            continue;
        }

        pos_to_role[pos_role->Position] = *pos_role;
        pos_to_users[pos_role->Position].push_back(std::move(*user));
        if (col_role.has_value()) user_to_color[user_id] = col_role->Color;
    }

    const auto add_user = [this, &guild, &user_to_color](const UserData &user, const Gtk::TreeRow &parent) {
        auto test = m_model->append(parent->children());
        auto row = *test;
        row[m_columns.m_type] = MemberListRenderType::Member;
        row[m_columns.m_id] = user.ID;
        row[m_columns.m_name] = user.GetDisplayNameEscaped();
        row[m_columns.m_pixbuf] = Abaddon::Get().GetImageManager().GetPlaceholder(16);
        row[m_columns.m_av_requested] = false;
        if (const auto iter = user_to_color.find(user.ID); iter != user_to_color.end()) {
            row[m_columns.m_color] = IntToRGBA(iter->second);
        } else {
            const static auto transparent = Gdk::RGBA("rgba(0,0,0,0)");
            row[m_columns.m_color] = transparent;
        }
        m_pending_avatars[user.ID] = test;
        return test;
    };

    const auto add_role = [this](const RoleData &role) {
        auto row = *m_model->append();
        row[m_columns.m_type] = MemberListRenderType::Role;
        row[m_columns.m_id] = role.ID;
        row[m_columns.m_name] = "<b>" + role.GetEscapedName() + "</b>";
        return row;
    };

    for (auto it = pos_to_role.crbegin(); it != pos_to_role.crend(); it++) {
        const auto pos = it->first;
        const auto &role = it->second;

        auto role_row = add_role(role);

        if (pos_to_users.find(pos) == pos_to_users.end()) continue;

        auto &users = pos_to_users.at(pos);
        AlphabeticalSort(users.begin(), users.end(), [](const auto &e) { return e.Username; });

        for (const auto &user : users) add_user(user, role_row);
    }

    auto everyone_role = *m_model->append();
    everyone_role[m_columns.m_type] = MemberListRenderType::Role;
    everyone_role[m_columns.m_id] = m_active_guild; // yes thats how the role works
    everyone_role[m_columns.m_name] = "<b>@everyone</b>";

    for (const auto id : roleless_users) {
        const auto user = discord.GetUser(id);
        if (user.has_value()) add_user(*user, everyone_role);
    }

    m_view.expand_all();
}

void MemberList::Clear() {
    m_model->clear();
    m_pending_avatars.clear();
}

void MemberList::SetActiveChannel(Snowflake id) {
    m_active_channel = id;
    m_active_guild = Snowflake::Invalid;
    if (m_active_channel.IsValid()) {
        const auto channel = Abaddon::Get().GetDiscordClient().GetChannel(m_active_channel);
        if (channel.has_value() && channel->GuildID.has_value()) m_active_guild = *channel->GuildID;
    }
}

void MemberList::OnCellRender(uint64_t id) {
    Snowflake real_id = id;
    if (const auto iter = m_pending_avatars.find(real_id); iter != m_pending_avatars.end()) {
        auto row = iter->second;
        m_pending_avatars.erase(iter);
        if (!row) return;
        if ((*row)[m_columns.m_av_requested]) return;
        (*row)[m_columns.m_av_requested] = true;
        const auto user = Abaddon::Get().GetDiscordClient().GetUser(real_id);
        if (!user.has_value()) return;
        const auto cb = [this, row](const Glib::RefPtr<Gdk::Pixbuf> &pb) {
            // for some reason row::operator bool() returns true when m_model->iter_is_valid returns false
            // idk why since other code already does essentially the same thing im doing here
            // iter_is_valid is "slow" according to gtk but the only other workaround i can think of would be worse
            if (row && m_model->iter_is_valid(row)) {
                (*row)[m_columns.m_pixbuf] = pb->scale_simple(16, 16, Gdk::INTERP_BILINEAR);
            }
        };
        Abaddon::Get().GetImageManager().LoadFromURL(user->GetAvatarURL("png", "16"), cb);
    }
}

MemberList::ModelColumns::ModelColumns() {
    add(m_type);
    add(m_id);
    add(m_name);
    add(m_pixbuf);
    add(m_av_requested);
    add(m_color);
}
