/**
 * @file
 * Automatically submit the payment redirect form.
 */

(function ($) {
  Drupal.behaviors.robokassaPayment = {
    attach: function (context, settings) {
      $('div.payment-tinkoff-redirect-form form', context).submit();
    }
  };
})(jQuery);
