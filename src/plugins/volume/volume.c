#include <gtk/gtk.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

#include "volume-impl.h"

#include "volume_xpm.h"

int mixer_fd;

typedef struct {
	GtkWidget *mainw;
	GtkWidget *dlg;
	GtkTooltips* tooltips;
} volume_t;

static void
volume_destructor(plugin *p)
{
	volume_t *vol = (volume_t *) p->priv;

	ENTER;
	if (vol->dlg)
		gtk_widget_destroy(vol->dlg);
	g_object_unref( vol->tooltips );
	gtk_widget_destroy(vol->mainw);
	if (mixer_fd)
		close(mixer_fd);
	g_free(vol);
	RET();
}

static void on_volume_focus (GtkWidget* dlg, GdkEventFocus *event, volume_t *vol)
{
	/* FIXME: use smarter method */
	gtk_widget_destroy( dlg );
	vol->dlg = NULL;
}

static void on_button_press (GtkWidget* widget, GdkEventButton* evt, plugin* p)
{
	volume_t *vol = (volume_t*) p->priv;

	if( evt->button == 1 ) { /*  Left click*/
		if( ! vol->dlg ) {
			vol->dlg = create_volume_window();
			g_signal_connect( vol->dlg, "focus-out-event", 
					  G_CALLBACK(on_volume_focus), vol );
		}
		gtk_window_present( GTK_WINDOW(vol->dlg) );
	}
}

static int
volume_constructor(plugin *p)
{
	volume_t *vol;
	line s;
	GdkPixbuf *icon;
	GtkWidget *image;
	GtkIconTheme* theme;
	GtkIconInfo* info;

	ENTER;
	s.len = 256;  
	vol = g_new0(volume_t, 1);
	g_return_val_if_fail(vol != NULL, 0);
	p->priv = vol;

	/* check if OSS mixer device could be open */
	mixer_fd = open ("/dev/mixer", O_RDWR, 0);
	if (mixer_fd < 0) {
		RET(0);
	}

	vol->mainw = gtk_event_box_new();

	theme = gtk_icon_theme_get_default();
	if ( (info = gtk_icon_theme_lookup_icon( theme, "stock_volume", 24, 0 ) ) ) {
		icon = gdk_pixbuf_new_from_file_at_size( 
				gtk_icon_info_get_filename( info ),
				24, 24, NULL );
		gtk_icon_info_free( info );
	}
	else
    		icon = gdk_pixbuf_new_from_xpm_data((const char **) volume_xpm);
	if (icon) {
		image = gtk_image_new_from_pixbuf(icon);
		gtk_container_add (GTK_CONTAINER (vol->mainw), image);
	}

	gtk_widget_add_events( vol->mainw, GDK_BUTTON_PRESS_MASK );
	g_signal_connect( vol->mainw, "button-press-event",
			G_CALLBACK(on_button_press), p );
	gtk_widget_set_size_request( vol->mainw, 24, 24 );

	vol->dlg = NULL;
	gtk_widget_show_all(vol->mainw);

	gtk_container_add(GTK_CONTAINER(p->pwid), vol->mainw);

	vol->tooltips = gtk_tooltips_new ();
#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink( vol->tooltips );
#else
    g_object_ref( vol->tooltips );
    gtk_object_sink( vol->tooltips );
#endif

	/* FIXME: display current level in tooltip. ex: "Volume Control: 80%"  */
	gtk_tooltips_set_tip (vol->tooltips, vol->mainw, _("Volume control"), NULL);

	RET(1);
}


plugin_class volume_plugin_class = {
	fname: NULL,
	count: 0,

	type : "volume",
    name : N_("Volume Control"),
	version: "1.0",
	description : "Display and control volume",

	constructor : volume_constructor,
	destructor  : volume_destructor,
    config : NULL
};