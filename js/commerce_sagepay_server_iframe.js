;(function($) {
  /**
   * Automatically submit the hidden form that points to the iframe.
   */
  Drupal.behaviors.commerceSagePayIFrame = {
    attach: function (context, settings) {
      if (top.location != location) {
        top.location.href = document.location.href;
      }
    }
  }
})(jQuery);
