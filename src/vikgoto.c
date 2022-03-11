/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 * Copyright (C) 2009, Guilhem Bonnefille <guilhem.bonnefille@gmail.com>
 * Copyright (C) 2020, Rob Norris <rw_norris@hotmail.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include "viking.h"
#include "vikgototool.h"
#include "vikgoto.h"
#include "background.h"

static gchar *last_goto_str = NULL;
static VikCoord *last_coord = NULL;
static gchar *last_successful_goto_str = NULL;

static GList *goto_tools_list = NULL;

#define VIK_SETTINGS_GOTO_PROVIDER "goto_provider"
int last_goto_tool = -1;

struct VikGotoSearchWinData {
  VikWindow *vw;
  VikViewport *vvp;
  VikLayersPanel *vlp;
  GtkWidget *dialog;
  GtkEntry *goto_entry;
  GtkWidget *tool_list;
  GtkWidget *scroll_view;
  GtkTreeView *results_view;
};

enum {
  VIK_GOTO_SEARCH_DESC_COL = 0,
  VIK_GOTO_SEARCH_LAT_COL,
  VIK_GOTO_SEARCH_LON_COL,
  VIK_GOTO_SEARCH_NUM_COLS
};

void vik_goto_register ( VikGotoTool *tool )
{
  if ( IS_VIK_GOTO_TOOL( tool ) )
    goto_tools_list = g_list_append ( goto_tools_list, g_object_ref ( tool ) );
}

void vik_goto_unregister_all ()
{
  g_list_foreach ( goto_tools_list, (GFunc) g_object_unref, NULL );
}

gchar * a_vik_goto_get_search_string_for_this_place(VikWindow *vw)
{
  if (!last_coord)
    return NULL;

  VikViewport *vvp = vik_window_viewport(vw);
  const VikCoord *cur_center = vik_viewport_get_center(vvp);
  if (vik_coord_equals(cur_center, last_coord)) {
    return(last_successful_goto_str);
  }
  else
    return NULL;
}

static void display_no_tool(VikWindow *vw)
{
  GtkWidget *dialog = NULL;

  dialog = gtk_message_dialog_new ( GTK_WINDOW(vw), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, _("No goto tool available.") );

  gtk_dialog_run ( GTK_DIALOG(dialog) );

  gtk_widget_destroy(dialog);
}

static gint find_entry = -1;
static gint wanted_entry = -1;

static void find_provider (gpointer elem, gpointer user_data)
{
  const gchar *name = vik_goto_tool_get_label (elem);
  const gchar *provider = user_data;
  find_entry++;
  if (!strcmp(name, provider)) {
    wanted_entry = find_entry;
  }
}

/**
 * Setup last_goto_tool value
 */
static void get_provider ()
{
  // Use setting for the provider if available
  if ( last_goto_tool < 0 ) {
    find_entry = -1;
    wanted_entry = -1;
    gchar *provider = NULL;
    if ( a_settings_get_string ( VIK_SETTINGS_GOTO_PROVIDER, &provider ) ) {
      // Use setting
      if ( provider )
        g_list_foreach (goto_tools_list, find_provider, provider);
      // If not found set it to the first entry, otherwise use the entry
      last_goto_tool = ( wanted_entry < 0 ) ? 0 : wanted_entry;
      g_free ( provider );
    }
    else
      last_goto_tool = 0;
  }
}

static void
text_changed_cb (GtkEntry   *entry,
                 GParamSpec *pspec,
                 GtkWidget  *button)
{
  gboolean has_text = gtk_entry_get_text_length(entry) > 0;
  gtk_entry_set_icon_sensitive ( entry, GTK_ENTRY_ICON_SECONDARY, has_text );
  gtk_widget_set_sensitive ( button, has_text );
}

/**
 * Goto a place when we already have a string to search on
 *
 * Returns: %TRUE if a successful lookup
 */
static gboolean vik_goto_place ( VikViewport *vvp, gchar* name, VikCoord *vcoord )
{
  // Ensure last_goto_tool is given a value
  get_provider ();

  if ( goto_tools_list ) {
    VikGotoTool *gototool = g_list_nth_data ( goto_tools_list, last_goto_tool );
    if ( gototool ) {
      if ( vik_goto_tool_get_coord ( gototool, vvp, name, vcoord ) == 0 )
        return TRUE;
    }
  }
  return FALSE;
}

static gboolean vik_goto_search_list_select ( GtkTreeSelection *sel, GtkTreeModel *model, GtkTreePath *path, gboolean path_currently_selected, gpointer pdata )
{
  VikLayersPanel *vlp = VIK_LAYERS_PANEL(pdata);
  GtkTreeIter iter;

  if ( gtk_tree_model_get_iter ( model, &iter, path ) )
  {
    gdouble lat;
    gdouble lon;

    gtk_tree_model_get ( model, &iter, VIK_GOTO_SEARCH_LAT_COL, &lat, -1 );
    gtk_tree_model_get ( model, &iter, VIK_GOTO_SEARCH_LON_COL, &lon, -1 );

    if ( last_coord )
      g_free ( last_coord );
    last_coord = g_malloc( sizeof(VikCoord) );

    struct LatLon ll = { lat, lon };
    vik_coord_load_from_latlon ( last_coord, VIK_COORD_LATLON, &ll );

    if ( last_successful_goto_str )
      g_free ( last_successful_goto_str );
    gtk_tree_model_get ( model, &iter, VIK_GOTO_SEARCH_DESC_COL, &last_successful_goto_str, -1 );

    vik_viewport_set_center_coord ( vik_layers_panel_get_viewport(vlp), last_coord, !path_currently_selected );
    vik_layers_panel_emit_update ( vlp, FALSE );
  }

  return TRUE;
}

static void vik_goto_search_response ( struct VikGotoSearchWinData *data, gint response )
{
  if ( response == GTK_RESPONSE_ACCEPT )
  {
    // TODO check if list is empty
    last_goto_tool = gtk_combo_box_get_active ( GTK_COMBO_BOX(data->tool_list) );
    gchar *provider = vik_goto_tool_get_label ( g_list_nth_data (goto_tools_list, last_goto_tool) );
    a_settings_set_string ( VIK_SETTINGS_GOTO_PROVIDER, provider );

    gchar *goto_str = g_strdup ( gtk_entry_get_text ( GTK_ENTRY(data->goto_entry) ) );

    if (goto_str[0] != '\0') {
      if ( last_goto_str )
        g_free ( last_goto_str );
      last_goto_str = g_strdup ( goto_str );
    }

    VikGotoTool *tool = g_list_nth_data ( goto_tools_list, last_goto_tool );

    GList *candidates = NULL;

    vik_window_set_busy_cursor_widget ( data->dialog, data->vw );
    int ans = vik_goto_tool_get_candidates ( tool, goto_str, &candidates );
    vik_window_clear_busy_cursor_widget ( data->dialog, data->vw );

    if ( ans == 0 ) {
      // make results visible
      gtk_widget_set_size_request( GTK_WIDGET(data->scroll_view), 320, 240 );
      gtk_widget_set_size_request( GTK_WIDGET(data->results_view), 320, 240 );
      gtk_widget_show ( data->scroll_view );

      GtkListStore *results_store = gtk_list_store_new ( VIK_GOTO_SEARCH_NUM_COLS,
                                                         G_TYPE_STRING,
                                                         G_TYPE_DOUBLE,
                                                         G_TYPE_DOUBLE );
      GtkTreeIter results_iter;

      for ( GList *l = candidates; l != NULL; l = l->next )
      {
        struct VikGotoCandidate *cand = (struct VikGotoCandidate *) l->data;
        gtk_list_store_append ( results_store, &results_iter );
        gtk_list_store_set ( results_store, &results_iter,
                             VIK_GOTO_SEARCH_DESC_COL, cand->description,
                             VIK_GOTO_SEARCH_LAT_COL, cand->ll.lat,
                             VIK_GOTO_SEARCH_LON_COL, cand->ll.lon,
                             -1 );
      }

      gtk_tree_view_set_model ( data->results_view, GTK_TREE_MODEL(results_store) );

      if ( g_list_length( candidates ) > 0 )
      {
        GtkTreeIter first_iter;
        gtk_tree_model_get_iter_first ( GTK_TREE_MODEL(results_store), &first_iter);
        GtkTreeSelection *selection = gtk_tree_view_get_selection( data->results_view );
        gtk_tree_selection_select_iter ( selection, &first_iter );
      }

      g_object_unref ( results_store );
      g_free ( goto_str );
      g_list_free_full ( candidates, vik_goto_tool_free_candidate );
    }
    else
    {
      a_dialog_error_msg ( GTK_WINDOW(data->vw), _("Service request failure.") );
    }
  }
  else if ( response == GTK_RESPONSE_CLOSE )
  {
    gtk_widget_destroy ( data->dialog );
    g_free( data );
  }
}

static void setup_columns ( GtkWidget *results_view, GtkWidget *scroll_view )
{
  gtk_container_add ( GTK_CONTAINER(scroll_view), results_view );
  gtk_scrolled_window_set_policy ( GTK_SCROLLED_WINDOW(scroll_view), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );

  GtkCellRenderer *desc_renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_insert_column_with_attributes ( GTK_TREE_VIEW(results_view),
                                                -1,
                                                _("Description"),
                                                desc_renderer,
                                                "text", VIK_GOTO_SEARCH_DESC_COL,
                                                NULL );

  GtkTreeViewColumn *lat_col;
  lat_col = gtk_tree_view_column_new_with_attributes ( "Latitude",
                                                       gtk_cell_renderer_text_new (),
                                                       "text", VIK_GOTO_SEARCH_LAT_COL,
                                                       NULL );
  gtk_tree_view_column_set_visible ( lat_col, FALSE );
  gtk_tree_view_append_column ( GTK_TREE_VIEW(results_view), lat_col );

  GtkTreeViewColumn *lon_col;
  lon_col = gtk_tree_view_column_new_with_attributes ( "Longitude",
                                                       gtk_cell_renderer_text_new (),
                                                       "text", VIK_GOTO_SEARCH_LON_COL,
                                                       NULL );
  gtk_tree_view_column_set_visible ( lon_col, FALSE );
  gtk_tree_view_append_column ( GTK_TREE_VIEW(results_view), lon_col );
}

void a_vik_goto ( VikWindow *vw, VikViewport *vvp )
{
  GtkWidget *dialog = NULL;

  if ( goto_tools_list == NULL )
  {
    /* Empty list */
    display_no_tool ( vw );
    return;
  }

  dialog = gtk_dialog_new_with_buttons ( "", GTK_WINDOW(vw), 0, 
                                         GTK_STOCK_FIND, GTK_RESPONSE_ACCEPT,
                                         GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                         NULL );
  gtk_window_set_transient_for ( GTK_WINDOW(dialog), GTK_WINDOW(vw) );
  gtk_window_set_title( GTK_WINDOW(dialog), _("goto") );

  GtkWidget *tool_label = gtk_label_new( _("goto provider:") );
  GtkWidget *tool_list = vik_combo_box_text_new ();
  GList *current = g_list_first ( goto_tools_list );
  while ( current != NULL ) {
    VikGotoTool *tool = current->data;
    vik_combo_box_text_append ( tool_list, vik_goto_tool_get_label(tool) );
    current = g_list_next ( current );
  }

  get_provider ();
  gtk_combo_box_set_active ( GTK_COMBO_BOX( tool_list ), last_goto_tool );

  GtkWidget *goto_label = gtk_label_new(_("Enter address or place name:"));
  GtkWidget *goto_entry = ui_entry_new ( last_goto_str, GTK_ENTRY_ICON_SECONDARY );

  // 'ok' when press return in the entry
  g_signal_connect_swapped ( goto_entry, "activate", G_CALLBACK(a_dialog_response_accept), dialog );

#if GTK_CHECK_VERSION (2,20,0)
  GtkWidget *search_button = gtk_dialog_get_widget_for_response ( GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT );
  text_changed_cb ( GTK_ENTRY(goto_entry), NULL, search_button );
  g_signal_connect ( goto_entry, "notify::text", G_CALLBACK(text_changed_cb), search_button );
#endif

  GtkWidget *results_view = gtk_tree_view_new ();
  GtkWidget *scroll_view = gtk_scrolled_window_new ( NULL, NULL );

  setup_columns ( results_view, scroll_view );

  gtk_widget_set_size_request( GTK_WIDGET(scroll_view), 0, 0 );

  struct VikGotoSearchWinData *win_data = g_malloc ( sizeof(struct VikGotoSearchWinData) );
  win_data->vw = vw;
  win_data->vvp = vvp;
  win_data->vlp = vik_window_layers_panel(vw);
  win_data->dialog = dialog;
  win_data->goto_entry = GTK_ENTRY(goto_entry);
  win_data->scroll_view = scroll_view;
  win_data->results_view = GTK_TREE_VIEW(results_view);
  win_data->tool_list = tool_list;

  GtkTreeSelection *selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW(results_view) );
  gtk_tree_selection_set_select_function ( selection, vik_goto_search_list_select, win_data->vlp, NULL );

  gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), tool_label, FALSE, FALSE, 5 );
  gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), tool_list, FALSE, FALSE, 5 );
  gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), goto_label, FALSE, FALSE, 5 );
  gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), goto_entry, FALSE, FALSE, 5 );
  gtk_box_pack_start ( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll_view, TRUE, TRUE, 5 );
  gtk_dialog_set_default_response ( GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT );
  g_signal_connect_swapped ( GTK_DIALOG(dialog), "response", G_CALLBACK(vik_goto_search_response), win_data );

  gtk_widget_show_all ( dialog );
  // don't show the scroll view until we have something to show
  gtk_widget_hide ( scroll_view );

  // Ensure the text field has focus so we can start typing straight away
  gtk_widget_grab_focus ( goto_entry );

  gtk_widget_show ( dialog );
}

//

#define VIK_GOTO_PANEL_TYPE            (vik_goto_panel_get_type ())
#define VIK_GOTO_PANEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), VIK_GOTO_PANEL_TYPE, VikGotoPanel))
#define VIK_GOTO_PANEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), VIK_GOTO_PANEL_TYPE, VikGotoPanelClass))
#define IS_VIK_GOTO_PANEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), VIK_GOTO_PANEL_TYPE))
#define IS_VIK_GOTO_PANEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), VIK_GOTO_PANEL_TYPE))

typedef struct _VikGotoPanel VikGotoPanel;
typedef struct _VikGotoPanelClass VikGotoPanelClass;

struct _VikGotoPanelClass
{
  GtkVBoxClass vbox_class;
};

GType vik_goto_panel_get_type ();

static GObjectClass *parent_class;

struct _VikGotoPanel {
#if GTK_CHECK_VERSION (3,0,0)
  GtkBox vb;
#else
  GtkVBox vb;
#endif
  GtkWidget *goto_entry;
  GtkWidget *tool_list;
  GtkWidget *find_button;
  GtkWidget *scroll_view;
  GtkTreeView *results_view;
  GtkListStore *results_store;
  VikLayersPanel *vlp;
};

static void vik_goto_panel_init ( VikGotoPanel *vgp )
{
#if GTK_CHECK_VERSION (3,0,0)
  // Force vertical mode for box (as default is horizontal in GTK3)
  gtk_orientable_set_orientation ( GTK_ORIENTABLE(vgp), GTK_ORIENTATION_VERTICAL );
#endif
}

static void goto_panel_finalize ( GObject *gob )
{
  VikGotoPanel *vgp = VIK_GOTO_PANEL ( gob );
  g_object_unref ( vgp->results_store );
  G_OBJECT_CLASS(parent_class)->finalize(gob);
}

G_DEFINE_TYPE (VikGotoPanel, vik_goto_panel, GTK_TYPE_VBOX)

static void vik_goto_panel_class_init ( VikGotoPanelClass *klass )
{
  GObjectClass *object_class;
  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = goto_panel_finalize;
  parent_class = g_type_class_peek_parent (klass);
}

VikGotoPanel *vik_goto_panel_new ()
{
  // Equivalent to gtk_vbox_new ( FALSE, 0 );
  return VIK_GOTO_PANEL ( g_object_new ( VIK_GOTO_PANEL_TYPE, "homogeneous", FALSE, "spacing", 0, NULL ) );
}

typedef struct {
  VikGotoTool *tool;
  int answer;
  GList *candidates;
  gchar *goto_str;
  VikViewport *vvp;
  VikGotoPanel *vgp;
  // Protection in case owning window is closed whilst thread is in progress
  // c.f. weak refs used in vikmapslayer.c
  gboolean alive;
  GMutex *mutex;
} SearchThreadT;

static void stt_free ( SearchThreadT *stt )
{
  vik_mutex_free ( stt->mutex );
  g_list_free_full ( stt->candidates, vik_goto_tool_free_candidate );
  g_free ( stt->goto_str );
  g_free ( stt );
}

static void weak_ref_cb ( gpointer ptr, GObject *obj )
{
  SearchThreadT *stt = ptr;
  g_mutex_lock ( stt->mutex );
  stt->alive = FALSE;
  g_mutex_unlock ( stt->mutex );
}

static void goto_panel_search_clear ( VikGotoPanel *vgp )
{
  if ( vgp->results_store )
    gtk_list_store_clear ( vgp->results_store );
  // Just incase find button is in a disabled state
  gtk_widget_set_sensitive ( vgp->find_button, TRUE );
}

static gboolean _idle_update ( gpointer user_data )
{
  SearchThreadT *stt = (SearchThreadT*)user_data;
  VikGotoPanel *vgp = stt->vgp;

  gtk_list_store_clear ( vgp->results_store );

  GtkTreeViewColumn *desc_col = gtk_tree_view_get_column ( GTK_TREE_VIEW(vgp->results_view), VIK_GOTO_SEARCH_DESC_COL );

  GtkTreeIter results_iter;
  for ( GList *gl = stt->candidates; gl != NULL; gl = gl->next ) {
    struct VikGotoCandidate *cand = (struct VikGotoCandidate *) gl->data;
    gtk_list_store_append ( vgp->results_store, &results_iter );
    gtk_list_store_set ( vgp->results_store, &results_iter,
                         VIK_GOTO_SEARCH_DESC_COL, cand->description,
                         VIK_GOTO_SEARCH_LAT_COL, cand->ll.lat,
                         VIK_GOTO_SEARCH_LON_COL, cand->ll.lon,
                         -1 );
  }

  if ( g_list_length( stt->candidates ) > 0 ) {
    gtk_tree_view_column_set_title ( desc_col, _("Description") );
    GtkTreeIter first_iter;
    gtk_tree_model_get_iter_first ( GTK_TREE_MODEL(vgp->results_store), &first_iter);
    GtkTreeSelection *selection = gtk_tree_view_get_selection( vgp->results_view );
    gtk_tree_selection_select_iter ( selection, &first_iter );
  }
  else
    gtk_tree_view_column_set_title ( desc_col, _("No results") );

  if ( stt->answer != 0 )
    gtk_tree_view_column_set_title ( desc_col, _("Service request failure") );

  gtk_widget_set_sensitive ( vgp->find_button, TRUE );

  stt_free ( stt );

  return FALSE;
}

/**
 * get_locations_thread:
 * @threaddata: Data used by our background thread mechanism
 *
 */
static int get_locations_thread ( SearchThreadT *stt, gpointer threaddata )
{
  // As only one event; no practical chance to request stop before it is started so ignore the answer
  (void)a_background_thread_progress ( threaddata, 0.0 );

  stt->answer = vik_goto_tool_get_candidates ( stt->tool, stt->goto_str, &(stt->candidates) );

  // Confirm window is still available
  g_mutex_lock ( stt->mutex );
  if ( stt->alive ) {
    // Since from a background thread
    (void)gdk_threads_add_idle ( _idle_update, stt );

    g_object_weak_unref ( G_OBJECT(stt->vgp->vlp), weak_ref_cb, stt );
  }
  g_mutex_unlock ( stt->mutex );

  return 0;
}

static void goto_panel_search_response ( VikGotoPanel *vgp )
{
  gint atool = gtk_combo_box_get_active ( GTK_COMBO_BOX(vgp->tool_list) );
  if ( atool < 0 ) {
    g_critical ( "%s: %s", __FUNCTION__, "No goto provider" );
    return;
  }

  // Use column title for status reporting
  GtkTreeViewColumn *desc_col = gtk_tree_view_get_column ( GTK_TREE_VIEW(vgp->results_view), VIK_GOTO_SEARCH_DESC_COL );
  gtk_tree_view_column_set_title ( desc_col, _("Searching...") );

  // Prevent further requests
  gtk_widget_set_sensitive ( vgp->find_button, FALSE );

  VikGotoTool *tool = g_list_nth_data ( goto_tools_list, atool );
  gchar *provider = vik_goto_tool_get_label ( tool );
  a_settings_set_string ( VIK_SETTINGS_GOTO_PROVIDER, provider );

  SearchThreadT *stt = g_malloc ( sizeof(SearchThreadT) );

  stt->tool = g_list_nth_data ( goto_tools_list, atool );
  stt->candidates = NULL;
  stt->vvp = vik_layers_panel_get_viewport ( vgp->vlp );
  stt->vgp = vgp;
  stt->goto_str = g_strdup ( gtk_entry_get_text ( GTK_ENTRY(vgp->goto_entry) ) );
  stt->alive = TRUE;
  stt->mutex = vik_mutex_new();

  gchar *msg = g_strdup_printf ( _("Goto request on: %s"), stt->goto_str );

  g_object_weak_ref ( G_OBJECT(vgp->vlp), weak_ref_cb, stt );

  a_background_thread ( BACKGROUND_POOL_REMOTE,
                        GTK_WINDOW(VIK_WINDOW(VIK_GTK_WINDOW_FROM_WIDGET(vgp->vlp))),
                        msg,
                        (vik_thr_func)get_locations_thread,
                        stt,
                        NULL, // Don't free, data still needed for idle_upate
                        NULL, // Nothing to do on thread cancel
                        1 );

  g_free ( msg );
}

/**
 *
 */
GtkWidget* vik_goto_panel_widget ( VikLayersPanel *vlp )
{
  VikGotoPanel *vgp = vik_goto_panel_new ();
  vgp->vlp = vlp;

  vgp->tool_list = vik_combo_box_text_new ();

  GList *current = g_list_first ( goto_tools_list );
  while ( current != NULL ) {
    VikGotoTool *tool = current->data;
    vik_combo_box_text_append ( vgp->tool_list, vik_goto_tool_get_label(tool) );
    current = g_list_next ( current );
  }

  get_provider ();
  gtk_combo_box_set_active ( GTK_COMBO_BOX(vgp->tool_list), last_goto_tool );

  vgp->goto_entry = ui_entry_new ( NULL, GTK_ENTRY_ICON_SECONDARY );

  gtk_widget_set_tooltip_text ( vgp->goto_entry, _("Enter address or place name:") );
  // 'find' when press return in the entry
  g_signal_connect_swapped ( vgp->goto_entry, "activate", G_CALLBACK(goto_panel_search_response), vgp );

  vgp->results_store = gtk_list_store_new ( VIK_GOTO_SEARCH_NUM_COLS,
                                            G_TYPE_STRING,
                                            G_TYPE_DOUBLE,
                                            G_TYPE_DOUBLE );

  GtkWidget *results_view = gtk_tree_view_new ();
  vgp->results_view = GTK_TREE_VIEW(results_view);
  vgp->scroll_view = gtk_scrolled_window_new ( NULL, NULL );

  setup_columns ( results_view, vgp->scroll_view );

  gtk_tree_view_set_model ( vgp->results_view, GTK_TREE_MODEL(vgp->results_store) );

  GtkTreeSelection *selection = gtk_tree_view_get_selection ( vgp->results_view );
  gtk_tree_selection_set_select_function ( selection, vik_goto_search_list_select, vlp, NULL );

  GtkWidget *hb = gtk_hbox_new ( TRUE, 5 );
  vgp->find_button = gtk_button_new_from_stock ( GTK_STOCK_FIND );
  GtkWidget *clear_button = gtk_button_new_from_stock ( GTK_STOCK_CLEAR );
  gtk_box_pack_start ( GTK_BOX(hb), vgp->find_button, FALSE, FALSE, 0 );
  gtk_box_pack_start ( GTK_BOX(hb), clear_button, FALSE, FALSE, 0 );

#if GTK_CHECK_VERSION (2,20,0)
  text_changed_cb ( GTK_ENTRY(vgp->goto_entry), NULL, vgp->find_button );
  g_signal_connect ( GTK_ENTRY(vgp->goto_entry), "notify::text", G_CALLBACK(text_changed_cb), vgp->find_button );
#endif

  g_signal_connect_swapped ( vgp->find_button, "clicked", G_CALLBACK(goto_panel_search_response), vgp );
  g_signal_connect_swapped ( clear_button, "clicked", G_CALLBACK(goto_panel_search_clear), vgp );

  // Put the entry first so it is auto selected when the tab is entered,
  //  and so one can start typing straight away.
  gtk_box_pack_start ( GTK_BOX(vgp), vgp->goto_entry, FALSE, FALSE, 2 );
  gtk_box_pack_start ( GTK_BOX(vgp), vgp->tool_list, FALSE, FALSE, 2 );
  gtk_box_pack_start ( GTK_BOX(vgp), hb, FALSE, FALSE, 2 );
  gtk_box_pack_start ( GTK_BOX(vgp), vgp->scroll_view, TRUE, TRUE, 2 );

  return GTK_WIDGET(vgp);
}

#define JSON_LATITUDE_PATTERN "\"geoplugin_latitude\":\""
#define JSON_LONGITUDE_PATTERN "\"geoplugin_longitude\":\""
#define JSON_CITY_PATTERN "\"geoplugin_city\":\""
#define JSON_COUNTRY_PATTERN "\"geoplugin_countryName\":\""

/**
 * Automatic attempt to find out where you are using:
 *   1. http://www.geoplugin.com ++
 *   2. if not specific enough fallback to using the default goto tool with a country name
 * ++ Using returned JSON information
 *  c.f. with googlesearch.c - similar implementation is used here
 *
 * returns:
 *   0 if failed to locate anything
 *   1 if exact latitude/longitude found
 *   2 if position only as precise as a city
 *   3 if position only as precise as a country
 * @name: Contains the name of place found. Free this string after use.
 */
gint a_vik_goto_where_am_i ( VikViewport *vvp, struct LatLon *ll, gchar **name )
{
  gint result = 0;
  *name = NULL;

  gchar *tmpname = a_download_uri_to_tmp_file ( "http://www.geoplugin.net/json.gp", NULL );
  //gchar *tmpname = g_strdup ("../test/www.geoplugin.net-slash-json.gp.result");
  if (!tmpname) {
    return result;
  }

  ll->lat = 0.0;
  ll->lon = 0.0;

  gchar *pat;
  GMappedFile *mf;
  gchar *ss;
  gint fragment_len;

  gchar lat_buf[32], lon_buf[32];
  lat_buf[0] = lon_buf[0] = '\0';
  gchar *country = NULL;
  gchar *city = NULL;

  if ((mf = g_mapped_file_new(tmpname, FALSE, NULL)) == NULL) {
    g_critical(_("couldn't map temp file"));
    goto tidy;
  }

  gsize len = g_mapped_file_get_length(mf);
  gchar *text = g_mapped_file_get_contents(mf);

  if ((pat = g_strstr_len(text, len, JSON_COUNTRY_PATTERN))) {
    pat += strlen(JSON_COUNTRY_PATTERN);
    fragment_len = 0;
    ss = pat;
    while (*pat != '"') {
      fragment_len++;
      pat++;
    }
    country = g_strndup(ss, fragment_len);
  }

  if ((pat = g_strstr_len(text, len, JSON_CITY_PATTERN))) {
    pat += strlen(JSON_CITY_PATTERN);
    fragment_len = 0;
    ss = pat;
    while (*pat != '"') {
      fragment_len++;
      pat++;
    }
    city = g_strndup(ss, fragment_len);
  }

  if ((pat = g_strstr_len(text, len, JSON_LATITUDE_PATTERN))) {
    pat += strlen(JSON_LATITUDE_PATTERN);
    ss = lat_buf;
    if (*pat == '-')
      *ss++ = *pat++;
    while ((ss < (lat_buf + sizeof(lat_buf))) && (pat < (text + len)) &&
	   (g_ascii_isdigit(*pat) || (*pat == '.')))
      *ss++ = *pat++;
    *ss = '\0';
    ll->lat = g_ascii_strtod(lat_buf, NULL);
  }

  if ((pat = g_strstr_len(text, len, JSON_LONGITUDE_PATTERN))) {
    pat += strlen(JSON_LONGITUDE_PATTERN);
    ss = lon_buf;
    if (*pat == '-')
      *ss++ = *pat++;
    while ((ss < (lon_buf + sizeof(lon_buf))) && (pat < (text + len)) &&
	   (g_ascii_isdigit(*pat) || (*pat == '.')))
      *ss++ = *pat++;
    *ss = '\0';
    ll->lon = g_ascii_strtod(lon_buf, NULL);
  }

  if ( ll->lat != 0.0 && ll->lon != 0.0 ) {
    if ( ll->lat > -90.0 && ll->lat < 90.0 && ll->lon > -180.0 && ll->lon < 180.0 ) {
      // Found a 'sensible' & 'precise' location
      result = 1;
      *name = g_strdup ( _("Locality") ); //Albeit maybe not known by an actual name!
    }
  }
  else {
    // Hopefully city name is unique enough to lookup position on
    // For American places the service may append the State code on the end
    // But if the country code is not appended if could easily get confused
    //  e.g. 'Portsmouth' could be at least
    //   Portsmouth, Hampshire, UK or
    //   Portsmouth, Viginia, USA.

    // Try city name lookup
    if ( city ) {
      g_debug ( "%s: found city %s", __FUNCTION__, city );
      if ( strcmp ( city, "(Unknown city)" ) != 0 ) {
        VikCoord new_center;
        if ( vik_goto_place ( vvp, city, &new_center ) ) {
          // Got something
          vik_coord_to_latlon ( &new_center, ll );
          result = 2;
          *name = city;
          goto tidy;
        }
      }
    }

    // Try country name lookup
    if ( country ) {
      g_debug ( "%s: found country %s", __FUNCTION__, country );
      if ( strcmp ( country, "(Unknown Country)" ) != 0 ) {
        VikCoord new_center;
        if ( vik_goto_place ( vvp, country, &new_center ) ) {
          // Finally got something
          vik_coord_to_latlon ( &new_center, ll );
          result = 3;
          *name = country;
          goto tidy;
        }
      }
    }
  }
  
 tidy:
  g_mapped_file_unref ( mf );
  (void)util_remove ( tmpname );
  g_free ( tmpname );
  return result;
}
