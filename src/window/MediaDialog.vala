/*  This file is part of Cawbird, a Gtk+ linux Twitter client forked from Corebird.
 *  Copyright (C) 2013 Timm Bäder (Corebird)
 *
 *  Cawbird is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Cawbird is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with cawbird.  If not, see <http://www.gnu.org/licenses/>.
 */

[GtkTemplate (ui = "/uk/co/ibboard/cawbird/ui/media-dialog.ui")]
class MediaDialog : Gtk.Window {
  [GtkChild]
  private Gtk.Frame frame;
  [GtkChild]
  private Gtk.Revealer next_revealer;
  [GtkChild]
  private Gtk.Revealer previous_revealer;
  private unowned Cb.Media[] media;
  private int cur_index = 0;
  private Gtk.GestureMultiPress button_gesture;
  private Gdk.Rectangle max_dimensions;

  public MediaDialog (Cb.Media[] media,
                      int      start_media_index,
                      Gdk.Rectangle max_dimensions) {
    this.media = media;
    Cb.Media cur_media = media[start_media_index];
    this.cur_index = start_media_index;
    this.max_dimensions = max_dimensions;
    this.button_gesture = new Gtk.GestureMultiPress (this);
    this.button_gesture.set_button (0);
    this.button_gesture.set_propagation_phase (Gtk.PropagationPhase.BUBBLE);
    this.button_gesture.released.connect (button_released_cb);

    if (media.length == 1) {
      next_revealer.hide ();
      previous_revealer.hide ();
    }

    change_media (cur_media);
  }

  private void button_released_cb (int    n_press,
                                   double x,
                                   double y) {
    this.destroy ();
    button_gesture.set_state (Gtk.EventSequenceState.CLAIMED);
  }

  private void change_media (Cb.Media media) {
    /* Remove the current child */
    var cur_child = frame.get_child ();
    int cur_width = 0, cur_height = 0,
        new_width, new_height;


    if (frame.get_child () != null) {
      frame.remove (cur_child);
      cur_child.get_size_request (out cur_width, out cur_height);
    }

    Gtk.Widget new_widget = null;
    if (media.is_video ()) {
      new_widget = new Cb.MediaVideoWidget (media, max_dimensions);
      frame.add (new_widget);
      ((Cb.MediaVideoWidget)new_widget).start ();
    } else {
      new_widget = new Cb.MediaImageWidget (media, max_dimensions);
      frame.add (new_widget);
    }

    new_widget.show_all ();

    new_widget.get_size_request (out new_width, out new_height);
    if ((new_width != cur_width ||
        new_height != cur_height) && new_width > 0 && new_height > 0) {
      this.resize (new_width, new_height);
    }
    this.queue_resize ();

    next_revealer.set_visible (cur_index != this.media.length - 1);
    previous_revealer.set_visible (cur_index != 0);
    this.set_position(Gtk.WindowPosition.CENTER_ON_PARENT);
  }

  private void next_media () {
    if (cur_index < media.length - 1) {
      cur_index ++;
      change_media (media[cur_index]);
    }
  }

  private void previous_media () {
    if (cur_index > 0) {
      cur_index --;
      change_media (media[cur_index]);
    }
  }

  [GtkCallback]
  private bool key_press_event_cb (Gdk.EventKey evt) {
    if (evt.keyval == Gdk.Key.Left)
      previous_media ();
    else if (evt.keyval == Gdk.Key.Right)
      next_media ();
    else
      this.destroy ();

    return Gdk.EVENT_PROPAGATE;
  }

  [GtkCallback]
  private void next_button_clicked_cb () {
    next_media ();
  }

  [GtkCallback]
  private void previous_button_clicked_cb () {
    previous_media ();
  }

  public override bool enter_notify_event (Gdk.EventCrossing event) {
    if (event.window == this.get_window () &&
        event.detail != Gdk.NotifyType.INFERIOR) {
      next_revealer.reveal_child = true;
      previous_revealer.reveal_child = true;
    }

    return false;
  }

  public override bool leave_notify_event (Gdk.EventCrossing event) {
    if (event.window == this.get_window () &&
        event.detail != Gdk.NotifyType.INFERIOR) {
      next_revealer.reveal_child = false;
      previous_revealer.reveal_child = false;
    }

    return false;
  }
}
