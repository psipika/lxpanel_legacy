/**
 * Thermal plugin to lxpanel
 *
 * Copyright (C) 2007 by Daniel Kesler <kesler.daniel@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>

#include <string.h>

#include "plugin.h"
#include "misc.h"

#include "dbg.h"

#define PROC_THERMAL_DIRECTORY "/proc/acpi/thermal_zone/" /* must be slash-terminated */
#define PROC_THERMAL_TEMPF  "temperature"
#define PROC_THERMAL_TRIP  "trip_points"
#define PROC_TRIP_CRITICAL "critical (S5):"

#define SYSFS_THERMAL_DIRECTORY "/sys/class/thermal/" /* must be slash-terminated */
#define SYSFS_THERMAL_SUBDIR_PREFIX "thermal_zone"
#define SYSFS_THERMAL_TEMPF  "temp"
#define SYSFS_THERMAL_TRIP  "trip_point_0_temp"

#define MAX_NUM_SENSORS 10
#define MAX_AUTOMATIC_CRITICAL_TEMP 150 /* in degrees Celsius */


typedef struct thermal {
    Panel *panel;
    config_setting_t *settings;
    GtkWidget *namew;
    GString *tip;
    int critical;
    int warning1;
    int warning2;
    int not_custom_levels, auto_sensor;
    char *sensor,
         *str_cl_normal,
         *str_cl_warning1,
         *str_cl_warning2;
    unsigned int timer;
    GdkColor cl_normal,
             cl_warning1,
             cl_warning2;
    int numsensors;
    char *sensor_array[MAX_NUM_SENSORS];
    gint (*get_temperature[MAX_NUM_SENSORS])(char const* sensor_path);
    gint (*get_critical[MAX_NUM_SENSORS])(char const* sensor_path);
    gint temperature[MAX_NUM_SENSORS];
} thermal;


static gint
proc_get_critical(char const* sensor_path){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(sensor_path == NULL) return -1;

    snprintf(sstmp,sizeof(sstmp),"%s%s",sensor_path,PROC_THERMAL_TRIP);

    if (!(state = fopen( sstmp, "r"))) {
        ERR("thermal: cannot open %s\n", sstmp);
        return -1;
    }

    while( fgets(buf, 256, state) &&
            ! ( pstr = strstr(buf, PROC_TRIP_CRITICAL) ) );
    if( pstr )
    {
        pstr += strlen(PROC_TRIP_CRITICAL);
        while( *pstr && *pstr == ' ' )
            ++pstr;

        pstr[strlen(pstr)-3] = '\0';
        fclose(state);
        return atoi(pstr);
    }

    fclose(state);
    return -1;
}

static gint
proc_get_temperature(char const* sensor_path){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(sensor_path == NULL) return -1;

    snprintf(sstmp,sizeof(sstmp),"%s%s",sensor_path,PROC_THERMAL_TEMPF);

    if (!(state = fopen( sstmp, "r"))) {
        ERR("thermal: cannot open %s\n", sstmp);
        return -1;
    }

    while( fgets(buf, 256, state) &&
            ! ( pstr = strstr(buf, "temperature:") ) );
    if( pstr )
    {
        pstr += 12;
        while( *pstr && *pstr == ' ' )
            ++pstr;

        pstr[strlen(pstr)-3] = '\0';
        fclose(state);
        return atoi(pstr);
    }

    fclose(state);
    return -1;
}

static gint
sysfs_get_critical(char const* sensor_path){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(sensor_path == NULL) return -1;

    snprintf(sstmp,sizeof(sstmp),"%s%s",sensor_path,SYSFS_THERMAL_TRIP);

    if (!(state = fopen( sstmp, "r"))) {
        ERR("thermal: cannot open %s\n", sstmp);
        return -1;
    }

    while( fgets(buf, 256, state) &&
            ! ( pstr = buf ) );
    if( pstr )
    {
        fclose(state);
        return atoi(pstr)/1000;
    }

    fclose(state);
    return -1;
}

static gint
sysfs_get_temperature(char const* sensor_path){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(sensor_path == NULL) return -1;

    snprintf(sstmp,sizeof(sstmp),"%s%s",sensor_path,SYSFS_THERMAL_TEMPF);

    if (!(state = fopen( sstmp, "r"))) {
        ERR("thermal: cannot open %s\n", sstmp);
        return -1;
    }

    while (fgets(buf, 256, state) &&
	   ! ( pstr = buf ) );
    if( pstr )
    {
        fclose(state);
        return atoi(pstr)/1000;
    }

    fclose(state);
    return -1;
}

static gboolean
is_sysfs(char const* path)
{
    return path && strncmp(path, "/sys/", 5) == 0;
}

/* get_temp_function():
 *      - This function is called 'get_temp_function'.
 *      - It takes 'path' as argument.
 *      - It returns a pointer to a function.
 *      - The returned function takes a pointer to a char const as arg.
 *      - The returned function returns a gint. */
static gint (*get_temp_function(char const* path))(char const* sensor_path)
{
    if (is_sysfs(path))
        return sysfs_get_temperature;
    else
        return proc_get_temperature;
}

static gint (*get_crit_function(char const* path))(char const* sensor_path)
{
    if (is_sysfs(path))
        return sysfs_get_critical;
    else
        return proc_get_critical;
}

static gint get_temperature(thermal *th)
{
    gint max = -273;
    gint cur, i;

    for(i = 0; i < th->numsensors; i++){
        cur = th->get_temperature[i](th->sensor_array[i]);
        if (cur > max)
            max = cur;
        th->temperature[i] = cur;
    }

    return max;
}

static gint get_critical(thermal *th)
{
    gint min = MAX_AUTOMATIC_CRITICAL_TEMP;
    gint cur, i;

    for(i = 0; i < th->numsensors; i++){
        cur = th->get_critical[i](th->sensor_array[i]);
        if (cur < min)
            min = cur;
    }

    return min;
}

static void
update_display(thermal *th)
{
    char buffer [60];
    int i;
    int temp;
    GdkColor color;
    gchar *separator;

    temp = get_temperature(th);
    if(temp >= th->warning2)
        color = th->cl_warning2;
    else if(temp >= th->warning1)
        color = th->cl_warning1;
    else
        color = th->cl_normal;

    if(temp == -1)
        panel_draw_label_text(th->panel, th->namew, "NA", TRUE, 1, TRUE);
    else
    {
        snprintf(buffer, sizeof(buffer), "<span color=\"#%06x\"><b>%02d</b></span>",
                 gcolor2rgb24(&color), temp);
        gtk_label_set_markup (GTK_LABEL(th->namew), buffer) ;
    }

    g_string_truncate(th->tip, 0);
    separator = "";
    for (i = 0; i < th->numsensors; i++){
        g_string_append_printf(th->tip, "%s%s:\t%2d°C", separator, th->sensor_array[i], th->temperature[i]);
        separator = "\n";
    }
    gtk_widget_set_tooltip_text(th->namew, th->tip->str);
}

static gboolean update_display_timeout(gpointer user_data)
{
    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;
    update_display(user_data);
    return TRUE; /* repeat later */
}

static int
add_sensor(thermal* th, char const* sensor_path)
{
    if (th->numsensors + 1 > MAX_NUM_SENSORS){
        ERR("thermal: Too many sensors (max %d), ignoring '%s'\n",
                MAX_NUM_SENSORS, sensor_path);
        return -1;
    }

    th->sensor_array[th->numsensors] = g_strdup(sensor_path);
    th->get_critical[th->numsensors] = get_crit_function(sensor_path);
    th->get_temperature[th->numsensors] = get_temp_function(sensor_path);
    th->numsensors++;

    LOG(LOG_ALL, "thermal: Added sensor %s\n", sensor_path);

    return 0;
}

/* find_sensors():
 *      - Get the sensor directory, and store it in '*sensor'.
 *      - It is searched for in 'directory'.
 *      - Only the subdirectories starting with 'subdir_prefix' are accepted as sensors.
 *      - 'subdir_prefix' may be NULL, in which case any subdir is considered a sensor. */
static void
find_sensors(thermal* th, char const* directory, char const* subdir_prefix)
{
    GDir *sensorsDirectory;
    const char *sensor_name;
    char sensor_path[100];

    if (! (sensorsDirectory = g_dir_open(directory, 0, NULL)))
        return;

    /* Scan the thermal_zone directory for available sensors */
    while ((sensor_name = g_dir_read_name(sensorsDirectory))) {
        if (sensor_name[0] == '.')
            continue;
        if (subdir_prefix) {
            if (strncmp(sensor_name, subdir_prefix, strlen(subdir_prefix)) != 0)
                continue;
        }
        snprintf(sensor_path,sizeof(sensor_path),"%s%s/", directory, sensor_name);
        add_sensor(th, sensor_path);
    }
    g_dir_close(sensorsDirectory);
}

static void
remove_all_sensors(thermal *th)
{
    int i;

    LOG(LOG_ALL, "thermal: Removing all sensors (%d)\n", th->numsensors);

    for (i = 0; i < th->numsensors; i++)
        g_free(th->sensor_array[i]);

    th->numsensors = 0;
}

static void
check_sensors( thermal *th )
{
    find_sensors(th, PROC_THERMAL_DIRECTORY, NULL);
    find_sensors(th, SYSFS_THERMAL_DIRECTORY, SYSFS_THERMAL_SUBDIR_PREFIX);
    LOG(LOG_INFO, "thermal: Found %d sensors\n", th->numsensors);
}


static gboolean applyConfig(gpointer p)
{
    thermal *th = lxpanel_plugin_get_data(p);
    ENTER;

    if (th->str_cl_normal) gdk_color_parse(th->str_cl_normal, &th->cl_normal);
    if (th->str_cl_warning1) gdk_color_parse(th->str_cl_warning1, &th->cl_warning1);
    if (th->str_cl_warning2) gdk_color_parse(th->str_cl_warning2, &th->cl_warning2);

    remove_all_sensors(th);
    if(th->sensor == NULL) th->auto_sensor = TRUE;
    if(th->auto_sensor) check_sensors(th);
    else add_sensor(th, th->sensor);

    th->critical = get_critical(th);

    if(th->not_custom_levels){
        th->warning1 = th->critical - 10;
        th->warning2 = th->critical - 5;
    }

    config_group_set_string(th->settings, "NormalColor", th->str_cl_normal);
    config_group_set_string(th->settings, "Warning1Color", th->str_cl_warning1);
    config_group_set_string(th->settings, "Warning2Color", th->str_cl_warning2);
    config_group_set_int(th->settings, "CustomLevels", th->not_custom_levels);
    config_group_set_int(th->settings, "Warning1Temp", th->warning1);
    config_group_set_int(th->settings, "Warning2Temp", th->warning2);
    config_group_set_int(th->settings, "AutomaticSensor", th->auto_sensor);
    config_group_set_string(th->settings, "Sensor", th->sensor);
    RET(FALSE);
}

static void
thermal_destructor(gpointer user_data)
{
  thermal *th = (thermal *)user_data;

  ENTER;
  remove_all_sensors(th);
  g_string_free(th->tip, TRUE);
  g_free(th->sensor);
  g_free(th->str_cl_normal);
  g_free(th->str_cl_warning1);
  g_free(th->str_cl_warning2);
  g_source_remove(th->timer);
  g_free(th);
  RET();
}

static GtkWidget *
thermal_constructor(Panel *panel, config_setting_t *settings)
{
    thermal *th;
    GtkWidget *p;
    const char *tmp;

    ENTER;
    th = g_new0(thermal, 1);
    th->panel = panel;
    th->settings = settings;

    p = gtk_event_box_new();
    lxpanel_plugin_set_data(p, th, thermal_destructor);
    GTK_WIDGET_SET_FLAGS( p, GTK_NO_WINDOW );
    gtk_container_set_border_width( GTK_CONTAINER(p), 2 );

    th->namew = gtk_label_new("ww");
    gtk_container_add(GTK_CONTAINER(p), th->namew);

    th->tip = g_string_new(NULL);

    /* By default, use automatic, that is, "not custom" temperature levels. If
     * we were using custom levels, they would be 0°C at startup, so we would
     * display in warning colors by default. */
    th->not_custom_levels = TRUE;

    if (config_setting_lookup_string(settings, "NormalColor", &tmp))
        th->str_cl_normal = g_strdup(tmp);
    if (config_setting_lookup_string(settings, "Warning1Color", &tmp))
        th->str_cl_warning1 = g_strdup(tmp);
    if (config_setting_lookup_string(settings, "Warning2Color", &tmp))
        th->str_cl_warning2 = g_strdup(tmp);
    config_setting_lookup_int(settings, "AutomaticSensor", &th->auto_sensor);
    config_setting_lookup_int(settings, "CustomLevels", &th->not_custom_levels);
    if (config_setting_lookup_string(settings, "Sensor", &tmp))
        th->sensor = g_strdup(tmp);
    config_setting_lookup_int(settings, "Warning1Temp", &th->warning1);
    config_setting_lookup_int(settings, "Warning2Temp", &th->warning2);

    if(!th->str_cl_normal)
        th->str_cl_normal = g_strdup("#00ff00");
    if(!th->str_cl_warning1)
        th->str_cl_warning1 = g_strdup("#fff000");
    if(!th->str_cl_warning2)
        th->str_cl_warning2 = g_strdup("#ff0000");

    applyConfig(p);

    gtk_widget_show(th->namew);

    update_display(th);
    th->timer = g_timeout_add_seconds(3, (GSourceFunc) update_display_timeout, (gpointer)th);

    RET(p);
}

static GtkWidget *config(Panel *panel, GtkWidget *p, GtkWindow *parent)
{
    ENTER;

    GtkWidget *dialog;
    thermal *th = lxpanel_plugin_get_data(p);
    dialog = lxpanel_generic_config_dlg(_("Temperature Monitor"),
            panel, applyConfig, p,
            _("Normal"), &th->str_cl_normal, CONF_TYPE_STR,
            _("Warning1"), &th->str_cl_warning1, CONF_TYPE_STR,
            _("Warning2"), &th->str_cl_warning2, CONF_TYPE_STR,
            _("Automatic sensor location"), &th->auto_sensor, CONF_TYPE_BOOL,
            _("Sensor"), &th->sensor, CONF_TYPE_STR,
            _("Automatic temperature levels"), &th->not_custom_levels, CONF_TYPE_BOOL,
            _("Warning1 Temperature"), &th->warning1, CONF_TYPE_INT,
            _("Warning2 Temperature"), &th->warning2, CONF_TYPE_INT,
            NULL);

    RET(dialog);
}

FM_DEFINE_MODULE(lxpanel_gtk, thermal)

LXPanelPluginInit fm_module_init_lxpanel_gtk = {
    .name = N_("Temperature Monitor"),
    .description = N_("Display system temperature"),

    .new_instance = thermal_constructor,
    .config = config,
};


/* vim: set sw=4 sts=4 et : */
