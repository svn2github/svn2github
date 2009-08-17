#include "config.h"

#ifdef __linux__

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "callbacks.h"
#include "interface.h"
#include "support.h"

#ifdef ENABLE_NLS
#include <libintl.h>
#include <locale.h>
#define _(x) gettext(x)
#else
#define _(x) (x)
#endif

#define GLADE_HOOKUP_OBJECT(component,widget,name) \
  g_object_set_data_full (G_OBJECT (component), name, \
    gtk_widget_ref (widget), (GDestroyNotify) gtk_widget_unref)

#define GLADE_HOOKUP_OBJECT_NO_REF(component,widget,name) \
  g_object_set_data (G_OBJECT (component), name, widget)

GtkWidget*
create_cfg_dialog (void)
{
  GtkWidget *cfg_dialog;
  GtkWidget *dialog_vbox1;
  GtkWidget *vbox1;
  GtkWidget *frame1;
  GtkWidget *cddev_combo;
  GtkWidget *cddev_entry;
  GtkWidget *cdr_label;
  GtkWidget *frame2;
  GtkWidget *vbox2;
  GtkWidget *hbox1;
  GtkWidget *readmode_label;
  GtkWidget *readmode_optionmenu;
  GtkWidget *menu1;
  GtkWidget *normal;
  GtkWidget *threaded;
  GtkWidget *hseparator1;
  GtkWidget *hbox2;
  GtkWidget *label4;
  GtkObject *spinCacheSize_adj;
  GtkWidget *spinCacheSize;
  GtkWidget *hseparator2;
  GtkWidget *hbox3;
  GtkWidget *label5;
  GtkObject *spinCdrSpeed_adj;
  GtkWidget *spinCdrSpeed;
  GtkWidget *cfg_hseparator;
  AtkObject *atko;
  GtkWidget *subQ_button;
  GtkWidget *options_label;
  GtkWidget *cfg_dialog_action_area;
  GtkWidget *cfg_cancelbutton;
  GtkWidget *cfg_okbutton;
  GtkTooltips *tooltips;

  tooltips = gtk_tooltips_new ();

  cfg_dialog = gtk_dialog_new ();
  gtk_container_set_border_width (GTK_CONTAINER (cfg_dialog), 5);
  gtk_window_set_title (GTK_WINDOW (cfg_dialog), _("CDR configuration"));
  gtk_window_set_position (GTK_WINDOW (cfg_dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_modal (GTK_WINDOW (cfg_dialog), TRUE);
  gtk_dialog_set_has_separator (GTK_DIALOG (cfg_dialog), FALSE);

  dialog_vbox1 = GTK_DIALOG (cfg_dialog)->vbox;
  gtk_widget_show (dialog_vbox1);

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox1);
  gtk_box_pack_start (GTK_BOX (dialog_vbox1), vbox1, TRUE, TRUE, 0);

  frame1 = gtk_frame_new (NULL);
  gtk_widget_show (frame1);
  gtk_box_pack_start (GTK_BOX (vbox1), frame1, TRUE, TRUE, 0);

  cddev_combo = gtk_combo_new ();
  g_object_set_data (G_OBJECT (GTK_COMBO (cddev_combo)->popwin),
                     "GladeParentKey", cddev_combo);
  gtk_widget_show (cddev_combo);
  gtk_container_add (GTK_CONTAINER (frame1), cddev_combo);
  gtk_container_set_border_width (GTK_CONTAINER (cddev_combo), 10);

  cddev_entry = GTK_COMBO (cddev_combo)->entry;
  gtk_widget_show (cddev_entry);
  gtk_tooltips_set_tip (tooltips, cddev_entry, _("Choose your CD-ROM device or type its path if it's not listed"), NULL);
  gtk_entry_set_activates_default (GTK_ENTRY (cddev_entry), TRUE);

  cdr_label = gtk_label_new (_("Select CD-ROM device"));
  gtk_widget_show (cdr_label);
  gtk_frame_set_label_widget (GTK_FRAME (frame1), cdr_label);
  gtk_label_set_justify (GTK_LABEL (cdr_label), GTK_JUSTIFY_LEFT);

  frame2 = gtk_frame_new (NULL);
  gtk_widget_show (frame2);
  gtk_box_pack_start (GTK_BOX (vbox1), frame2, TRUE, TRUE, 0);

  vbox2 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox2);
  gtk_container_add (GTK_CONTAINER (frame2), vbox2);

  hbox1 = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (hbox1);
  gtk_box_pack_start (GTK_BOX (vbox2), hbox1, FALSE, FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox1), 5);

  readmode_label = gtk_label_new (_("Select read mode:"));
  gtk_widget_show (readmode_label);
  gtk_box_pack_start (GTK_BOX (hbox1), readmode_label, FALSE, FALSE, 0);
  gtk_label_set_justify (GTK_LABEL (readmode_label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_padding (GTK_MISC (readmode_label), 5, 5);

  readmode_optionmenu = gtk_option_menu_new ();
  gtk_widget_show (readmode_optionmenu);
  gtk_box_pack_start (GTK_BOX (hbox1), readmode_optionmenu, TRUE, TRUE, 1);
  gtk_container_set_border_width (GTK_CONTAINER (readmode_optionmenu), 5);

  menu1 = gtk_menu_new ();

  normal = gtk_menu_item_new_with_mnemonic (_("Normal (No Cache)"));
  gtk_widget_show (normal);
  gtk_container_add (GTK_CONTAINER (menu1), normal);

  threaded = gtk_menu_item_new_with_mnemonic (_("Threaded - Faster (With Cache)"));
  gtk_widget_show (threaded);
  gtk_container_add (GTK_CONTAINER (menu1), threaded);

  gtk_option_menu_set_menu (GTK_OPTION_MENU (readmode_optionmenu), menu1);

  hseparator1 = gtk_hseparator_new ();
  gtk_widget_show (hseparator1);
  gtk_box_pack_start (GTK_BOX (vbox2), hseparator1, TRUE, TRUE, 0);

  hbox2 = gtk_hbox_new (FALSE, 5);
  gtk_widget_show (hbox2);
  gtk_box_pack_start (GTK_BOX (vbox2), hbox2, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox2), 5);

  label4 = gtk_label_new (_("Cache Size (Def. 64):                  "));
  gtk_widget_show (label4);
  gtk_box_pack_start (GTK_BOX (hbox2), label4, FALSE, FALSE, 0);
  gtk_label_set_justify (GTK_LABEL (label4), GTK_JUSTIFY_LEFT);

//  spinCacheSize_adj = gtk_adjustment_new (32, 32, 2048, 1, 16, 16);
  spinCacheSize_adj = gtk_adjustment_new (32, 32, 2048, 1, 16, 0);
  spinCacheSize = gtk_spin_button_new (GTK_ADJUSTMENT (spinCacheSize_adj), 1, 0);
  gtk_widget_show (spinCacheSize);
  gtk_box_pack_start (GTK_BOX (hbox2), spinCacheSize, TRUE, TRUE, 0);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinCacheSize), TRUE);

  hseparator2 = gtk_hseparator_new ();
  gtk_widget_show (hseparator2);
  gtk_box_pack_start (GTK_BOX (vbox2), hseparator2, TRUE, TRUE, 0);

  hbox3 = gtk_hbox_new (FALSE, 5);
  gtk_widget_show (hbox3);
  gtk_box_pack_start (GTK_BOX (vbox2), hbox3, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox3), 5);

  label5 = gtk_label_new (_("Cdrom Speed (Def. 0 = MAX):    "));
  gtk_widget_show (label5);
  gtk_box_pack_start (GTK_BOX (hbox3), label5, FALSE, FALSE, 0);
  gtk_label_set_justify (GTK_LABEL (label5), GTK_JUSTIFY_LEFT);

//  spinCdrSpeed_adj = gtk_adjustment_new (0, 0, 100, 1, 4, 4);
  spinCdrSpeed_adj = gtk_adjustment_new (0, 0, 100, 1, 4, 0);
  spinCdrSpeed = gtk_spin_button_new (GTK_ADJUSTMENT (spinCdrSpeed_adj), 1, 0);
  gtk_widget_show (spinCdrSpeed);
  gtk_box_pack_start (GTK_BOX (hbox3), spinCdrSpeed, TRUE, TRUE, 0);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinCdrSpeed), TRUE);

  cfg_hseparator = gtk_hseparator_new ();
  gtk_widget_show (cfg_hseparator);
  gtk_box_pack_start (GTK_BOX (vbox2), cfg_hseparator, TRUE, TRUE, 0);

  subQ_button = gtk_check_button_new_with_mnemonic (_("Enable Subchannel read"));
  gtk_widget_show (subQ_button);
  gtk_box_pack_start (GTK_BOX (vbox2), subQ_button, FALSE, FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (subQ_button), 10);

  options_label = gtk_label_new (_("Options"));
  gtk_widget_show (options_label);
  gtk_frame_set_label_widget (GTK_FRAME (frame2), options_label);
  gtk_label_set_justify (GTK_LABEL (options_label), GTK_JUSTIFY_LEFT);

  cfg_dialog_action_area = GTK_DIALOG (cfg_dialog)->action_area;
  gtk_widget_show (cfg_dialog_action_area);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (cfg_dialog_action_area), GTK_BUTTONBOX_END);

  cfg_cancelbutton = gtk_button_new_from_stock ("gtk-cancel");
  gtk_widget_show (cfg_cancelbutton);
  gtk_dialog_add_action_widget (GTK_DIALOG (cfg_dialog), cfg_cancelbutton, GTK_RESPONSE_CANCEL);
  GTK_WIDGET_SET_FLAGS (cfg_cancelbutton, GTK_CAN_DEFAULT);

  cfg_okbutton = gtk_button_new_from_stock ("gtk-ok");
  gtk_widget_show (cfg_okbutton);
  gtk_dialog_add_action_widget (GTK_DIALOG (cfg_dialog), cfg_okbutton, GTK_RESPONSE_OK);
  GTK_WIDGET_SET_FLAGS (cfg_okbutton, GTK_CAN_DEFAULT);

  g_signal_connect ((gpointer) cfg_dialog, "show",
                    G_CALLBACK (on_cfg_dialog_show),
                    NULL);
  g_signal_connect ((gpointer) cfg_cancelbutton, "clicked",
                    G_CALLBACK (on_cfg_cancelbutton_clicked),
                    NULL);
  g_signal_connect ((gpointer) cfg_dialog, "delete_event",
                    G_CALLBACK (on_cfg_cancelbutton_clicked),
                    NULL);
  g_signal_connect ((gpointer) cfg_okbutton, "clicked",
                    G_CALLBACK (on_cfg_okbutton_clicked),
                    NULL);

  atko = gtk_widget_get_accessible (cfg_hseparator);
  atk_object_set_name (atko, "hseparator");

  /* Store pointers to all widgets, for use by lookup_widget(). */
  GLADE_HOOKUP_OBJECT_NO_REF (cfg_dialog, cfg_dialog, "cfg_dialog");
  GLADE_HOOKUP_OBJECT_NO_REF (cfg_dialog, dialog_vbox1, "dialog_vbox1");
  GLADE_HOOKUP_OBJECT (cfg_dialog, vbox1, "vbox1");
  GLADE_HOOKUP_OBJECT (cfg_dialog, frame1, "frame1");
  GLADE_HOOKUP_OBJECT (cfg_dialog, cddev_combo, "cddev_combo");
  GLADE_HOOKUP_OBJECT (cfg_dialog, cddev_entry, "cddev_entry");
  GLADE_HOOKUP_OBJECT (cfg_dialog, cdr_label, "cdr_label");
  GLADE_HOOKUP_OBJECT (cfg_dialog, frame2, "frame2");
  GLADE_HOOKUP_OBJECT (cfg_dialog, vbox2, "vbox2");
  GLADE_HOOKUP_OBJECT (cfg_dialog, hbox1, "hbox1");
  GLADE_HOOKUP_OBJECT (cfg_dialog, readmode_label, "readmode_label");
  GLADE_HOOKUP_OBJECT (cfg_dialog, readmode_optionmenu, "readmode_optionmenu");
  GLADE_HOOKUP_OBJECT (cfg_dialog, menu1, "menu1");
  GLADE_HOOKUP_OBJECT (cfg_dialog, normal, "normal");
  GLADE_HOOKUP_OBJECT (cfg_dialog, threaded, "threaded");
  GLADE_HOOKUP_OBJECT (cfg_dialog, hseparator1, "hseparator1");
  GLADE_HOOKUP_OBJECT (cfg_dialog, hbox2, "hbox2");
  GLADE_HOOKUP_OBJECT (cfg_dialog, label4, "label4");
  GLADE_HOOKUP_OBJECT (cfg_dialog, spinCacheSize, "spinCacheSize");
  GLADE_HOOKUP_OBJECT (cfg_dialog, hseparator2, "hseparator2");
  GLADE_HOOKUP_OBJECT (cfg_dialog, hbox3, "hbox3");
  GLADE_HOOKUP_OBJECT (cfg_dialog, label5, "label5");
  GLADE_HOOKUP_OBJECT (cfg_dialog, spinCdrSpeed, "spinCdrSpeed");
  GLADE_HOOKUP_OBJECT (cfg_dialog, cfg_hseparator, "cfg_hseparator");
  GLADE_HOOKUP_OBJECT (cfg_dialog, subQ_button, "subQ_button");
  GLADE_HOOKUP_OBJECT (cfg_dialog, options_label, "options_label");
  GLADE_HOOKUP_OBJECT_NO_REF (cfg_dialog, cfg_dialog_action_area, "cfg_dialog_action_area");
  GLADE_HOOKUP_OBJECT (cfg_dialog, cfg_cancelbutton, "cfg_cancelbutton");
  GLADE_HOOKUP_OBJECT (cfg_dialog, cfg_okbutton, "cfg_okbutton");
  GLADE_HOOKUP_OBJECT_NO_REF (cfg_dialog, tooltips, "tooltips");

  return cfg_dialog;
}

GtkWidget*
create_abt_dialog (void)
{
  GtkWidget *abt_dialog;
  GtkWidget *abt_dialog_vbox;
  GtkWidget *vbox3;
  GtkWidget *label3;
  GtkWidget *label1;
  GtkWidget *label2;
  GtkWidget *abt_dialog_action_area;
  GtkWidget *abt_okbutton;

  abt_dialog = gtk_dialog_new ();
  gtk_widget_set_size_request (abt_dialog, 300, 200);
  gtk_container_set_border_width (GTK_CONTAINER (abt_dialog), 10);
  gtk_window_set_title (GTK_WINDOW (abt_dialog), "About CDR");
  gtk_window_set_position (GTK_WINDOW (abt_dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_destroy_with_parent (GTK_WINDOW (abt_dialog), TRUE);

  abt_dialog_vbox = GTK_DIALOG (abt_dialog)->vbox;
  gtk_widget_show (abt_dialog_vbox);

  vbox3 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox3);
  gtk_box_pack_start (GTK_BOX (abt_dialog_vbox), vbox3, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox3), 10);

  label3 = gtk_label_new ("<span size=\"xx-large\"><b>CDR plugin</b></span>");
  gtk_widget_show (label3);
  gtk_box_pack_start (GTK_BOX (vbox3), label3, FALSE, FALSE, 0);
  gtk_label_set_use_markup (GTK_LABEL (label3), TRUE);
  gtk_label_set_justify (GTK_LABEL (label3), GTK_JUSTIFY_LEFT);
  gtk_misc_set_padding (GTK_MISC (label3), 5, 5);

  label1 = gtk_label_new ("linux CDR plugin for Pcsx\n\n");
  gtk_widget_show (label1);
  gtk_box_pack_start (GTK_BOX (vbox3), label1, FALSE, FALSE, 0);
  gtk_label_set_justify (GTK_LABEL (label1), GTK_JUSTIFY_FILL);
  gtk_misc_set_padding (GTK_MISC (label1), 5, 5);

  label2 = gtk_label_new ("<small>(c) linuzappz linuzappz@hotmail.com\n      xobro _xobro_@tin.it</small>\n");
  gtk_widget_show (label2);
  gtk_box_pack_start (GTK_BOX (vbox3), label2, FALSE, FALSE, 0);
  gtk_label_set_use_markup (GTK_LABEL (label2), TRUE);
  gtk_label_set_justify (GTK_LABEL (label2), GTK_JUSTIFY_LEFT);
  gtk_misc_set_padding (GTK_MISC (label2), 5, 5);

  abt_dialog_action_area = GTK_DIALOG (abt_dialog)->action_area;
  gtk_widget_show (abt_dialog_action_area);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (abt_dialog_action_area), GTK_BUTTONBOX_END);

  abt_okbutton = gtk_button_new_from_stock ("gtk-ok");
  gtk_widget_show (abt_okbutton);
  gtk_dialog_add_action_widget (GTK_DIALOG (abt_dialog), abt_okbutton, GTK_RESPONSE_OK);
  GTK_WIDGET_SET_FLAGS (abt_okbutton, GTK_CAN_DEFAULT);

  g_signal_connect ((gpointer) abt_okbutton, "clicked",
                    G_CALLBACK (on_abt_okbutton_clicked),
                    NULL);
  g_signal_connect ((gpointer) abt_dialog, "delete_event",
                    G_CALLBACK (on_abt_okbutton_clicked),
                    NULL);

  /* Store pointers to all widgets, for use by lookup_widget(). */
  GLADE_HOOKUP_OBJECT_NO_REF (abt_dialog, abt_dialog, "abt_dialog");
  GLADE_HOOKUP_OBJECT_NO_REF (abt_dialog, abt_dialog_vbox, "abt_dialog_vbox");
  GLADE_HOOKUP_OBJECT (abt_dialog, vbox3, "vbox3");
  GLADE_HOOKUP_OBJECT (abt_dialog, label3, "label3");
  GLADE_HOOKUP_OBJECT (abt_dialog, label1, "label1");
  GLADE_HOOKUP_OBJECT (abt_dialog, label2, "label2");
  GLADE_HOOKUP_OBJECT_NO_REF (abt_dialog, abt_dialog_action_area, "abt_dialog_action_area");
  GLADE_HOOKUP_OBJECT (abt_dialog, abt_okbutton, "abt_okbutton");

  return abt_dialog;
}

#endif
