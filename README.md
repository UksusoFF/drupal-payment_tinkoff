# Drupal Payment Tinkoff
![Drupal Payment Tinkoff](https://img.shields.io/badge/Drupal-7.x-blue.svg)

This module provide integration [Tinkoff](https://oplata.tinkoff.ru/) payments for using with Drupal Ubercart / Commerce through [Payment](https://www.drupal.org/project/payment) (7.x-1.x version) module.

Module based on [Robokassa Payment](https://www.drupal.org/project/robokassa_payment) integration and [Tinkoff Ubercart](https://oplata.tinkoff.ru/landing/develop/cms/ubercart) modules.

**Tested on Ubercart only.** If you have any questions feel free to [create new issue](https://github.com/UksusoFF/drupal-payment_tinkoff/issues/new).

# Registering with Tinkoff
Before you start the installation process you must register on https://oplata.tinkoff.ru/ and create your own merchant.

You will get "Terminal key", "Password" and other settings for your payment system.

# Installation and Configuration
1. Download the [latest version](https://github.com/UksusoFF/drupal-payment_tinkoff/archive/master.zip).
2. Upload the downloaded file to your webserver.
3. Unzip the package into your `drupal/sites/all/modules/` directory.
4. Rename the folder to `payment_tinkoff`.
5. Go to `https://yoursitename.com/admin/modules` and enable module.
6. Go to `https://yoursitename.com/admin/config/services/payment/method/add` and add Tinkoff payment method.
7. Setup the settings according your data from Tinkoff.

# Additional information
* Default Process (IPN) URL: https://yoursitename.com/payment_tinkoff/process
* Default Success URL: https://yoursitename.com/payment_tinkoff/success
* Default Fail URL: https://yoursitename.com/payment_tinkoff/fail

More API documentation can be found at [Tinkoff API](https://oplata.tinkoff.ru/landing/develop/documentation).

# Additional information
* Add switch between testing and release terminals.
