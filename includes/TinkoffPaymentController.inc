<?php

class TinkoffPaymentController extends PaymentMethodController
{
  /**
   * Tinkoff server payment URL.
   */
  const SERVER_URL = 'https://securepay.tinkoff.ru/rest/';

  /**
   * Default controller data.
   *
   * @var array
   */
  public $controller_data_defaults = [
    'TerminalLogin' => '',
    'TerminalPass' => '',
  ];

  /**
   * Payment method configuration form elements callback.
   *
   * @var string
   */
  public $payment_method_configuration_form_elements_callback = 'payment_tinkoff_payment_method_configuration_form_elements';

  /**
   * Payment configuration form elements callback.
   *
   * @var string
   */
  public $payment_configuration_form_elements_callback = 'payment_tinkoff_payment_configuration_form_elements';

  /**
   * RobokassaPaymentController constructor.
   */
  public function __construct()
  {
    $this->title = t('Tinkoff');
    $this->description = t('Tinkoff payment method.');
  }

  /**
   * Implements PaymentMethodController::execute().
   */
  public function execute(Payment $payment)
  {
    entity_save('payment', $payment);
    drupal_goto('tinkoff/redirect/' . $payment->pid);
  }

  /**
   * Payment form generator.
   *
   * @param \Payment $payment
   *   Payment object.
   *
   * @return array
   *   Payment form array.
   */
  static public function getPaymentForm(Payment $payment)
  {
    $form = [];
    $settings = $payment->method->controller_data;

    $tinkoff = new TinkoffMerchantAPI($settings['TerminalLogin'], $settings['TerminalPassword'], self::SERVER_URL);

    $request = json_decode($tinkoff->buildQuery('Init', [
      'OrderId'  => $payment->pid,
      'Amount'   => $payment->totalAmount(true) * 100,
      'DATA'	   => 'Description=' . drupal_substr($payment->description, 0, 64) . '|connection_type=drupal7ubercart'
    ]));

    $form['#action'] = $request->PaymentURL;

    return $form;
  }

  /**
   * Post data validator.
   *
   * @param array $data
   *   Post data.
   *
   * @return bool
   *   TRUE if post data is valid FALSE otherwise.
   */
  static public function validatePost(array $data, $is_interaction = true)
  {
    watchdog('payment_tinkoff', json_encode($data), [], WATCHDOG_DEBUG);

    // Exit now if the $_POST was empty.
    if (empty($data)) {
      watchdog('payment_tinkoff', 'Interaction URL accessed with no POST data submitted.', [], WATCHDOG_WARNING);
      return false;
    }

    // Exit now if any required keys are not exists in $_POST.
    $required_keys = ['OutSum', 'InvId'];
    if ($is_interaction) {
      $required_keys[] = 'SignatureValue';
    }
    $unavailable_required_keys = array_diff_key(array_flip($required_keys), $data);
    if (!empty($unavailable_required_keys)) {
      watchdog('payment_tinkoff', 'Missing POST keys. POST data: <pre>!data</pre>', ['!data' => print_r($unavailable_required_keys, true)], WATCHDOG_WARNING);
      return false;
    }

    $payment = entity_load_single('payment', $data['InvId']);
    if (!$payment) {
      watchdog('payment_tinkoff', 'Missing transaction id.  POST data: !data', ['!data' => print_r($data, true)], WATCHDOG_WARNING);
      return false;
    }

    $settings = $payment->method->controller_data;
    // Exit now if missing Checkout ID.
    if (empty($settings['TerminalLogin'])) {
      $info = [
        '!settings' => print_r($settings, 1),
        '!data' => print_r($data, true),
      ];
      watchdog('payment_tinkoff !data', 'Missing merchant ID.  POST data: <pre>!data</pre> <pre>!settings</pre>',
        $info, WATCHDOG_WARNING);
      return false;
    }

    if ($is_interaction) {
      if ($payment) {
        // Robokassa Signature.
        $robo_sign = $data['SignatureValue'];

        // Create own Signature.
        $signature_data = [
          $data['OutSum'],
          $data['InvId'],
          $payment->method->controller_data['pass2'],
        ];

        $sign = hash($settings['hash_type'], implode(':', $signature_data));

        // Exit now if missing Signature.
        if (drupal_strtoupper($robo_sign) != drupal_strtoupper($sign)) {
          watchdog('payment_tinkoff', 'Missing Signature.  POST data: !data', ['!data' => print_r($data, true)], WATCHDOG_WARNING);
          return false;
        }
      }
    }

    if ($payment->totalAmount(true) != $data['OutSum']) {
      watchdog('payment_tinkoff', 'Missing transaction id amount.  POST data: !data', ['!data' => print_r($data, true)], WATCHDOG_WARNING);
      return false;
    }

    return true;

    $orderId = intval($_POST['OrderId']);
    $order = uc_order_load($orderId);

    if ($order == FALSE) {
      watchdog('uc_tinkoff', 'Payment Notifiations attempted for non-existent order @order_id.', array('@order_id' => $orderId), WATCHDOG_ERROR);
      exit('NOTOK');
    }

    $_POST['Password'] = variable_get('uc_tinkoff_password');
    ksort($_POST);
    $postData = $_POST;
    $post_token = $postData['Token'];

    unset($postData['Token']);
    $values = implode('', array_values($postData));
    $token = hash('sha256', $values);

    if ($token != $post_token) {
      exit('NOTOK');
    }

    $payment_amount = $order->order_total;
    $order_status = '';
    $comment = '';

    if ($_POST['Status'] == 'AUTHORIZED' && $order->order_status == 'payment_received') exit('OK');

    switch ($_POST['Status']) {
      case 'AUTHORIZED':
        $order_status = 'pending';
        $comment = t('Tinkoff payment status is Authorized.');
        break;

      case 'CONFIRMED':
        $comment = t('Tinkoff order #order.', array('order' => check_plain($orderId)));
        uc_payment_enter($orderId, 'tinkoff', $payment_amount, $order->uid, NULL, $comment);
        uc_cart_complete_sale($order);
        uc_order_comment_save($orderId, 0, t('Tinkoff reported a payment of @amount RUB', array('@amount' => uc_currency_format($payment_amount, FALSE))));
        break;

      case 'CANCELED':
        $order_status = 'canceled';
        $comment = t('Tinkoff canceled payment');
        break;

      case 'REJECTED':
        $order_status = 'abandoned';
        $comment = t('Tinkoff was rejected payment');
        break;

      case 'REVERSED':
        $order_status = 'canceled';
        $comment = t('Tinkoff payment status is Reverted.');
        break;

      case 'REFUNDED':
        $comment = t('Tinkoff refunded for order #order.', array('order' => check_plain($orderId)));
        uc_payment_enter($orderId, 'tinkoff', $payment_amount, $order->uid, NULL, $comment);
        break;
    }

    if ($order_status) {
      uc_order_update_status($orderId, $order_status);
    }

    uc_order_comment_save($orderId, 0, $comment);

    exit('OK');
  }

}
