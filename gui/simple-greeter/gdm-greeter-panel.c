/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <gconf/gconf-client.h>

#include "gdm-languages.h"
#include "gdm-layouts.h"
#include "gdm-greeter-panel.h"
#include "gdm-clock-widget.h"
#include "gdm-language-option-widget.h"
#include "gdm-layout-option-widget.h"
#include "gdm-session-option-widget.h"
#include "gdm-timer.h"
#include "gdm-profile.h"

#include "na-tray.h"

#define GDM_GREETER_PANEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_PANEL, GdmGreeterPanelPrivate))

struct GdmGreeterPanelPrivate
{
        int                     monitor;
        GdkRectangle            geometry;
        GtkWidget              *hbox;
        GtkWidget              *alignment;
        GtkWidget              *option_hbox;
        GtkWidget              *hostname_label;
        GtkWidget              *clock;
        GtkWidget              *language_option_widget;
        GtkWidget              *layout_option_widget;
        GtkWidget              *session_option_widget;

        GdmTimer               *animation_timer;
        double                  progress;

        char                   *default_session_name;
        char                   *default_language_name;
};

enum {
        PROP_0,
        PROP_MONITOR
};

enum {
        LANGUAGE_SELECTED,
        LAYOUT_SELECTED,
        SESSION_SELECTED,
        NUMBER_OF_SIGNALS
};

static guint signals [NUMBER_OF_SIGNALS] = { 0, };

static void     gdm_greeter_panel_class_init  (GdmGreeterPanelClass *klass);
static void     gdm_greeter_panel_init        (GdmGreeterPanel      *greeter_panel);
static void     gdm_greeter_panel_finalize    (GObject              *object);

G_DEFINE_TYPE (GdmGreeterPanel, gdm_greeter_panel, GTK_TYPE_WINDOW)

static void
gdm_greeter_panel_set_monitor (GdmGreeterPanel *panel,
                               int              monitor)
{
        g_return_if_fail (GDM_IS_GREETER_PANEL (panel));

        if (panel->priv->monitor == monitor) {
                return;
        }

        panel->priv->monitor = monitor;

        gtk_widget_queue_resize (GTK_WIDGET (panel));

        g_object_notify (G_OBJECT (panel), "monitor");
}

static void
gdm_greeter_panel_set_property (GObject        *object,
                                guint           prop_id,
                                const GValue   *value,
                                GParamSpec     *pspec)
{
        GdmGreeterPanel *self;

        self = GDM_GREETER_PANEL (object);

        switch (prop_id) {
        case PROP_MONITOR:
                gdm_greeter_panel_set_monitor (self, g_value_get_int (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_panel_get_property (GObject        *object,
                                guint           prop_id,
                                GValue         *value,
                                GParamSpec     *pspec)
{
        GdmGreeterPanel *self;

        self = GDM_GREETER_PANEL (object);

        switch (prop_id) {
        case PROP_MONITOR:
                g_value_set_int (value, self->priv->monitor);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_greeter_panel_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
        GdmGreeterPanel      *greeter_panel;

        gdm_profile_start (NULL);

        greeter_panel = GDM_GREETER_PANEL (G_OBJECT_CLASS (gdm_greeter_panel_parent_class)->constructor (type,
                                                                                                         n_construct_properties,
                                                                                                         construct_properties));

        gdm_profile_end (NULL);

        return G_OBJECT (greeter_panel);
}

static void
gdm_greeter_panel_dispose (GObject *object)
{
        G_OBJECT_CLASS (gdm_greeter_panel_parent_class)->dispose (object);
}

/* copied from panel-toplevel.c */
static void
gdm_greeter_panel_move_resize_window (GdmGreeterPanel *panel,
                                      gboolean         move,
                                      gboolean         resize)
{
        GtkWidget *widget;

        widget = GTK_WIDGET (panel);

        g_assert (GTK_WIDGET_REALIZED (widget));

        if (move && resize) {
                gdk_window_move_resize (widget->window,
                                        panel->priv->geometry.x,
                                        panel->priv->geometry.y,
                                        panel->priv->geometry.width,
                                        panel->priv->geometry.height);
        } else if (move) {
                gdk_window_move (widget->window,
                                 panel->priv->geometry.x,
                                 panel->priv->geometry.y);
        } else if (resize) {
                gdk_window_resize (widget->window,
                                   panel->priv->geometry.width,
                                   panel->priv->geometry.height);
        }
}

static void
on_screen_size_changed (GdkScreen       *screen,
                        GdmGreeterPanel *panel)
{
        gtk_widget_queue_resize (GTK_WIDGET (panel));
}

static void
gdm_greeter_panel_real_realize (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->realize) {
                GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->realize (widget);
        }

        gdm_greeter_panel_move_resize_window (GDM_GREETER_PANEL (widget), TRUE, TRUE);

        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (widget)),
                          "size_changed",
                          G_CALLBACK (on_screen_size_changed),
                          widget);
}

static void
gdm_greeter_panel_real_unrealize (GtkWidget *widget)
{
        g_signal_handlers_disconnect_by_func (gtk_window_get_screen (GTK_WINDOW (widget)),
                                              on_screen_size_changed,
                                              widget);

        if (GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->unrealize) {
                GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->unrealize (widget);
        }
}

static void
set_struts (GdmGreeterPanel *panel,
            int              x,
            int              y,
            int              width,
            int              height)
{
        gulong        data[12] = { 0, };
        int           screen_height;

        /* _NET_WM_STRUT_PARTIAL: CARDINAL[12]/32
         *
         * 0: left          1: right       2:  top             3:  bottom
         * 4: left_start_y  5: left_end_y  6:  right_start_y   7:  right_end_y
         * 8: top_start_x   9: top_end_x   10: bottom_start_x  11: bottom_end_x
         *
         * Note: In xinerama use struts relative to combined screen dimensions,
         *       not just the current monitor.
         */

        /* for bottom panel */
        screen_height = gdk_screen_get_height (gtk_window_get_screen (GTK_WINDOW (panel)));

        /* bottom */
        data[3] = screen_height - panel->priv->geometry.y - panel->priv->geometry.height + height;
        /* bottom_start_x */
        data[10] = x;
        /* bottom_end_x */
        data[11] = x + width;

#if 0
        g_debug ("Setting strut: bottom=%lu bottom_start_x=%lu bottom_end_x=%lu", data[3], data[10], data[11]);
#endif

        gdk_error_trap_push ();

        gdk_property_change (GTK_WIDGET (panel)->window,
                             gdk_atom_intern ("_NET_WM_STRUT_PARTIAL", FALSE),
                             gdk_atom_intern ("CARDINAL", FALSE),
                             32,
                             GDK_PROP_MODE_REPLACE,
                             (guchar *) &data,
                             12);

        gdk_property_change (GTK_WIDGET (panel)->window,
                             gdk_atom_intern ("_NET_WM_STRUT", FALSE),
                             gdk_atom_intern ("CARDINAL", FALSE),
                             32,
                             GDK_PROP_MODE_REPLACE,
                             (guchar *) &data,
                             4);

        gdk_error_trap_pop ();
}

static void
update_struts (GdmGreeterPanel *panel)
{
        GdkRectangle geometry;

        gdk_screen_get_monitor_geometry (GTK_WINDOW (panel)->screen,
                                         panel->priv->monitor,
                                         &geometry);

        /* FIXME: assumes only one panel */
        set_struts (panel,
                    panel->priv->geometry.x,
                    panel->priv->geometry.y,
                    panel->priv->geometry.width,
                    panel->priv->geometry.height);
}

static void
update_geometry (GdmGreeterPanel *panel,
                 GtkRequisition  *requisition)
{
        GdkRectangle geometry;

        gdk_screen_get_monitor_geometry (GTK_WINDOW (panel)->screen,
                                         panel->priv->monitor,
                                         &geometry);

        panel->priv->geometry.width = geometry.width;
        panel->priv->geometry.height = requisition->height + 2 * GTK_CONTAINER (panel)->border_width;

        panel->priv->geometry.x = geometry.x;
        panel->priv->geometry.y = geometry.y + geometry.height - panel->priv->geometry.height + (1.0 - panel->priv->progress) * panel->priv->geometry.height;

#if 0
        g_debug ("Setting geometry x:%d y:%d w:%d h:%d",
                 panel->priv->geometry.x,
                 panel->priv->geometry.y,
                 panel->priv->geometry.width,
                 panel->priv->geometry.height);
#endif

        update_struts (panel);
}

static void
gdm_greeter_panel_real_size_request (GtkWidget      *widget,
                                     GtkRequisition *requisition)
{
        GdmGreeterPanel *panel;
        GtkBin          *bin;
        GdkRectangle     old_geometry;
        int              position_changed = FALSE;
        int              size_changed = FALSE;

        panel = GDM_GREETER_PANEL (widget);
        bin = GTK_BIN (widget);

        if (bin->child && GTK_WIDGET_VISIBLE (bin->child)) {
                gtk_widget_size_request (bin->child, requisition);
        }

        old_geometry = panel->priv->geometry;

        update_geometry (panel, requisition);

        requisition->width  = panel->priv->geometry.width;
        requisition->height = panel->priv->geometry.height;

        if (! GTK_WIDGET_REALIZED (widget)) {
                return;
        }

        if (old_geometry.width  != panel->priv->geometry.width ||
            old_geometry.height != panel->priv->geometry.height) {
                size_changed = TRUE;
        }

        if (old_geometry.x != panel->priv->geometry.x ||
            old_geometry.y != panel->priv->geometry.y) {
                position_changed = TRUE;
        }

        gdm_greeter_panel_move_resize_window (panel, position_changed, size_changed);
}
static void
gdm_greeter_panel_real_show (GtkWidget *widget)
{
        GdmGreeterPanel *panel;
        GtkSettings *settings;
        gboolean     animations_are_enabled;

        settings = gtk_settings_get_for_screen (gtk_widget_get_screen (widget));
        g_object_get (settings, "gtk-enable-animations", &animations_are_enabled, NULL);

        panel = GDM_GREETER_PANEL (widget);

        if (animations_are_enabled) {
                gdm_timer_start (panel->priv->animation_timer, 1.0);
        } else {
                panel->priv->progress = 1.0;
        }

        GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->show (widget);
}

static void
gdm_greeter_panel_real_hide (GtkWidget *widget)
{
        GdmGreeterPanel *panel;

        panel = GDM_GREETER_PANEL (widget);

        gdm_timer_stop (panel->priv->animation_timer);
        panel->priv->progress = 0.0;

        GTK_WIDGET_CLASS (gdm_greeter_panel_parent_class)->hide (widget);
}

static void
gdm_greeter_panel_class_init (GdmGreeterPanelClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->get_property = gdm_greeter_panel_get_property;
        object_class->set_property = gdm_greeter_panel_set_property;
        object_class->constructor = gdm_greeter_panel_constructor;
        object_class->dispose = gdm_greeter_panel_dispose;
        object_class->finalize = gdm_greeter_panel_finalize;

        widget_class->realize = gdm_greeter_panel_real_realize;
        widget_class->unrealize = gdm_greeter_panel_real_unrealize;
        widget_class->size_request = gdm_greeter_panel_real_size_request;
        widget_class->show = gdm_greeter_panel_real_show;
        widget_class->hide = gdm_greeter_panel_real_hide;

        signals[LANGUAGE_SELECTED] =
                g_signal_new ("language-selected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterPanelClass, language_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        signals[LAYOUT_SELECTED] =
                g_signal_new ("layout-selected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterPanelClass, layout_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        signals[SESSION_SELECTED] =
                g_signal_new ("session-selected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmGreeterPanelClass, session_selected),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_MONITOR,
                                         g_param_spec_int ("monitor",
                                                           "Xinerama monitor",
                                                           "The monitor (in terms of Xinerama) which the window is on",
                                                           0,
                                                           G_MAXINT,
                                                           0,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GdmGreeterPanelPrivate));
}

static void
on_language_activated (GdmLanguageOptionWidget *widget,
                       GdmGreeterPanel         *panel)
{

        char *language;

        language = gdm_language_option_widget_get_current_language_name (GDM_LANGUAGE_OPTION_WIDGET (panel->priv->language_option_widget));

        if (language == NULL) {
                return;
        }

        g_signal_emit (panel, signals[LANGUAGE_SELECTED], 0, language);

        g_free (language);
}

static void
on_layout_activated (GdmLayoutOptionWidget *widget,
                     GdmGreeterPanel       *panel)
{

        char *layout;

        layout = gdm_layout_option_widget_get_current_layout_name (GDM_LAYOUT_OPTION_WIDGET (panel->priv->layout_option_widget));

        if (layout == NULL) {
                return;
        }

        g_debug ("GdmGreeterPanel: activating selected layout %s", layout);
        gdm_layout_activate (layout);

        g_signal_emit (panel, signals[LAYOUT_SELECTED], 0, layout);

        g_free (layout);
}
static void
on_session_activated (GdmSessionOptionWidget *widget,
                      GdmGreeterPanel        *panel)
{

        char *session;

        session = gdm_session_option_widget_get_current_session (GDM_SESSION_OPTION_WIDGET (panel->priv->session_option_widget));

        if (session == NULL) {
                return;
        }

        g_signal_emit (panel, signals[SESSION_SELECTED], 0, session);

        g_free (session);
}

static void
on_animation_tick (GdmGreeterPanel *panel,
                   double           progress)
{
        panel->priv->progress = progress * log ((G_E - 1.0) * progress + 1.0);

        gtk_widget_queue_resize (GTK_WIDGET (panel));
}

static void
gdm_greeter_panel_init (GdmGreeterPanel *panel)
{
        NaTray    *tray;
        GtkWidget *spacer;

        gdm_profile_start (NULL);

        panel->priv = GDM_GREETER_PANEL_GET_PRIVATE (panel);

        GTK_WIDGET_SET_FLAGS (GTK_WIDGET (panel), GTK_CAN_FOCUS);

        panel->priv->geometry.x      = -1;
        panel->priv->geometry.y      = -1;
        panel->priv->geometry.width  = -1;
        panel->priv->geometry.height = -1;

        gtk_window_set_title (GTK_WINDOW (panel), _("Panel"));
        gtk_window_set_decorated (GTK_WINDOW (panel), FALSE);

        gtk_window_set_keep_above (GTK_WINDOW (panel), TRUE);
        gtk_window_set_type_hint (GTK_WINDOW (panel), GDK_WINDOW_TYPE_HINT_DOCK);
        gtk_window_set_opacity (GTK_WINDOW (panel), 0.85);

        panel->priv->hbox = gtk_hbox_new (FALSE, 12);
        gtk_container_set_border_width (GTK_CONTAINER (panel->priv->hbox), 0);
        gtk_widget_show (panel->priv->hbox);
        gtk_container_add (GTK_CONTAINER (panel), panel->priv->hbox);

        panel->priv->alignment = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_box_pack_start (GTK_BOX (panel->priv->hbox), panel->priv->alignment, TRUE, TRUE, 0);
        gtk_widget_show (panel->priv->alignment);

        panel->priv->option_hbox = gtk_hbox_new (FALSE, 12);
        gtk_widget_show (panel->priv->option_hbox);
        gtk_container_add (GTK_CONTAINER (panel->priv->alignment), panel->priv->option_hbox);

        spacer = gtk_label_new ("");
        gtk_box_pack_start (GTK_BOX (panel->priv->option_hbox), spacer, TRUE, TRUE, 6);
        gtk_widget_show (spacer);

        gdm_profile_start ("creating option widget");
        panel->priv->language_option_widget = gdm_language_option_widget_new ();
        g_signal_connect (G_OBJECT (panel->priv->language_option_widget),
                          "language-activated",
                          G_CALLBACK (on_language_activated), panel);
        gtk_box_pack_start (GTK_BOX (panel->priv->option_hbox), panel->priv->language_option_widget, FALSE, FALSE, 6);
        gdm_profile_end ("creating option widget");

        panel->priv->layout_option_widget = gdm_layout_option_widget_new ();
        g_signal_connect (G_OBJECT (panel->priv->layout_option_widget),
                          "layout-activated",
                          G_CALLBACK (on_layout_activated), panel);
        gtk_box_pack_start (GTK_BOX (panel->priv->option_hbox), panel->priv->layout_option_widget, FALSE, FALSE, 6);

        panel->priv->session_option_widget = gdm_session_option_widget_new ();
        g_signal_connect (G_OBJECT (panel->priv->session_option_widget),
                          "session-activated",
                          G_CALLBACK (on_session_activated), panel);
        gtk_box_pack_start (GTK_BOX (panel->priv->option_hbox), panel->priv->session_option_widget, FALSE, FALSE, 6);

        spacer = gtk_label_new ("");
        gtk_box_pack_start (GTK_BOX (panel->priv->option_hbox), spacer, TRUE, TRUE, 6);
        gtk_widget_show (spacer);

        /* FIXME: we should only show hostname on panel when connected
           to a remote host */
        if (0) {
                panel->priv->hostname_label = gtk_label_new (g_get_host_name ());
                gtk_box_pack_start (GTK_BOX (panel->priv->hbox), panel->priv->hostname_label, FALSE, FALSE, 6);
                gtk_widget_show (panel->priv->hostname_label);
        }

        panel->priv->clock = gdm_clock_widget_new ();
        gtk_box_pack_end (GTK_BOX (panel->priv->hbox),
                            GTK_WIDGET (panel->priv->clock), FALSE, FALSE, 6);
        gtk_widget_show (panel->priv->clock);

        tray = na_tray_new_for_screen (gtk_window_get_screen (GTK_WINDOW (panel)),
                                       GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_end (GTK_BOX (panel->priv->hbox), GTK_WIDGET (tray), FALSE, FALSE, 6);
        gtk_widget_show (GTK_WIDGET (tray));

        gdm_greeter_panel_hide_user_options (panel);

        panel->priv->progress = 0.0;
        panel->priv->animation_timer = gdm_timer_new ();
        g_signal_connect_swapped (panel->priv->animation_timer,
                                  "tick",
                                  G_CALLBACK (on_animation_tick),
                                  panel);

        gdm_profile_end (NULL);
}

static void
gdm_greeter_panel_finalize (GObject *object)
{
        GdmGreeterPanel *greeter_panel;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_PANEL (object));

        greeter_panel = GDM_GREETER_PANEL (object);

        g_return_if_fail (greeter_panel->priv != NULL);

        g_signal_handlers_disconnect_by_func (object, on_animation_tick, greeter_panel);
        g_object_unref (greeter_panel->priv->animation_timer);

        G_OBJECT_CLASS (gdm_greeter_panel_parent_class)->finalize (object);
}

GtkWidget *
gdm_greeter_panel_new (GdkScreen *screen,
                       int        monitor)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_GREETER_PANEL,
                               "screen", screen,
                               "monitor", monitor,
                               NULL);

        return GTK_WIDGET (object);
}

void
gdm_greeter_panel_show_user_options (GdmGreeterPanel *panel)
{
        gtk_widget_show (panel->priv->session_option_widget);
        gtk_widget_show (panel->priv->language_option_widget);
        gtk_widget_show (panel->priv->layout_option_widget);
}

void
gdm_greeter_panel_hide_user_options (GdmGreeterPanel *panel)
{
        gtk_widget_hide (panel->priv->session_option_widget);
        gtk_widget_hide (panel->priv->language_option_widget);
        gtk_widget_hide (panel->priv->layout_option_widget);

        g_debug ("GdmGreeterPanel: activating default layout");
        gdm_layout_activate (NULL);
}

void
gdm_greeter_panel_reset (GdmGreeterPanel *panel)
{
        gdm_greeter_panel_set_default_language_name (panel, NULL);
        gdm_greeter_panel_set_default_layout_name (panel, NULL);
        gdm_greeter_panel_set_default_session_name (panel, NULL);
        gdm_greeter_panel_hide_user_options (panel);
}

void
gdm_greeter_panel_set_default_language_name (GdmGreeterPanel *panel,
                                             const char      *language_name)
{
        char *normalized_language_name;

        g_return_if_fail (GDM_IS_GREETER_PANEL (panel));

        if (language_name != NULL) {
                normalized_language_name = gdm_normalize_language_name (language_name);
        } else {
                normalized_language_name = NULL;
        }

        if (normalized_language_name != NULL &&
            !gdm_option_widget_lookup_item (GDM_OPTION_WIDGET (panel->priv->language_option_widget),
                                            normalized_language_name, NULL, NULL, NULL)) {
                gdm_recent_option_widget_add_item (GDM_RECENT_OPTION_WIDGET (panel->priv->language_option_widget),
                                                   normalized_language_name);
        }

        gdm_option_widget_set_default_item (GDM_OPTION_WIDGET (panel->priv->language_option_widget),
                                            normalized_language_name);

        g_free (normalized_language_name);
}

void
gdm_greeter_panel_set_default_layout_name (GdmGreeterPanel *panel,
                                           const char      *layout_name)
{
#ifdef HAVE_LIBXKLAVIER
        g_return_if_fail (GDM_IS_GREETER_PANEL (panel));

        if (layout_name != NULL &&
            !gdm_layout_is_valid (layout_name)) {
                const char *default_layout;

                default_layout = gdm_layout_get_default_layout ();

                g_debug ("GdmGreeterPanel: default layout %s is invalid, resetting to: %s",
                         layout_name, default_layout ? default_layout : "null");

                g_signal_emit (panel, signals[LAYOUT_SELECTED], 0, default_layout);

                layout_name = default_layout;
        }

        if (layout_name != NULL &&
            !gdm_option_widget_lookup_item (GDM_OPTION_WIDGET (panel->priv->layout_option_widget),
                                            layout_name, NULL, NULL, NULL)) {
                gdm_recent_option_widget_add_item (GDM_RECENT_OPTION_WIDGET (panel->priv->layout_option_widget),
                                                   layout_name);
        }

        gdm_option_widget_set_default_item (GDM_OPTION_WIDGET (panel->priv->layout_option_widget),
                                            layout_name);

        g_debug ("GdmGreeterPanel: activating layout: %s", layout_name);
        gdm_layout_activate (layout_name);
#endif
}

void
gdm_greeter_panel_set_default_session_name (GdmGreeterPanel *panel,
                                            const char      *session_name)
{
        g_return_if_fail (GDM_IS_GREETER_PANEL (panel));

        if (session_name != NULL &&
            !gdm_option_widget_lookup_item (GDM_OPTION_WIDGET (panel->priv->session_option_widget),
                                            session_name, NULL, NULL, NULL)) {
                g_warning ("Default session is not available");
                return;
        }

        gdm_option_widget_set_default_item (GDM_OPTION_WIDGET (panel->priv->session_option_widget),
                                            session_name);
}