<?php

class TinkoffPaymentController extends PaymentMethodController
{
  /**
   * Default controller data.
   *
   * @var array
   */
  public $controller_data_defaults = [
    'mode' => 'test',
    'live_terminal_login' => '',
    'live_terminal_pass' => '',
    'test_terminal_login' => '',
    'test_terminal_pass' => '',
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
    drupal_goto('payment_tinkoff/redirect/' . $payment->pid);
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
    $settings = $payment->method->controller_data;

    $tinkoff = new TinkoffMerchantAPI(
      trim($settings[$settings['mode'] === 'test' ? 'test_terminal_login' : 'live_terminal_login']),
      trim($settings[$settings['mode'] === 'test' ? 'test_terminal_pass' : 'live_terminal_pass'])
    );

    try {
      payment_ubercart_order_id_load($payment);
      $order = uc_order_load($payment->payment_ubercart_uc_order_id);

      $nalog_system = null;

      foreach ($order->products as $product) {
        $product_node = node_load($product->nid);

        if (empty($product_node->field_product_nalog_system[LANGUAGE_NONE][0]['value'])) {
          throw new \Exception('Missed nalog system');
        }

        $product_nalog_system = $product_node->field_product_nalog_system[LANGUAGE_NONE][0]['value'];

        if ($nalog_system !== null && $nalog_system !== $product_nalog_system) {
          throw new \Exception('Various nalog system in one order');
        }

        $nalog_system = $product_nalog_system;
      }

      $query = [
        'Amount' => $payment->totalAmount(true) * 100,
        'OrderId' => $payment->pid,
        'Description' => "Оплата заказа {$order->order_id}",
      ];

      $items = [];
      foreach ($payment->getLineItems() as $item) {
        $price = $item->unitAmount(true);
        $name = trim(preg_replace("/&?[a-z0-9]+;/i", "", htmlspecialchars($item->description)));
        $quantity = round($item->quantity);

        $items[] = [
          'Name' => mb_substr($name, 0, 64),
          'Price' => round($price * 100),
          'Quantity' => $quantity,
          'Amount' => round($price * $quantity * 100),
          'PaymentMethod' => 'full_payment ',
          'PaymentObject' => 'service',
          'Tax' => 'none',
        ];
      }

      $query['Receipt'] = [
        'EmailCompany' => mb_substr(variable_get('site_mail', ''), 0, 64),
        'Email' => $order->primary_email,
        'Taxation' => $nalog_system,
        'Items' => $items,
      ];

      $request = json_decode($tinkoff->init($query), false);

      if (empty($request->PaymentURL)) {
        throw new \Exception('Missed PaymentURL. Response data: ' . json_encode($request, JSON_PRETTY_PRINT));
      }
    } catch (\Exception $e) {
      watchdog('payment_tinkoff', $e->getMessage(), [], WATCHDOG_ERROR);
      drupal_goto('payment_tinkoff/fail');
    }

    return [
      '#action' => $request->PaymentURL,
    ];
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
  static public function validatePost(array $data)
  {
    if (empty($data)) {
      watchdog('payment_tinkoff', 'Interaction URL accessed with no POST data submitted.', [], WATCHDOG_WARNING);

      return false;
    }

    $unavailable_required_keys = array_diff_key(array_flip([
      'TerminalKey',
      'OrderId',
      'Success',
      'Status',
      'PaymentId',
      'ErrorCode',
      'Amount',
      'Token',
    ]), $data);
    if (!empty($unavailable_required_keys)) {
      watchdog('payment_tinkoff', 'Missing POST keys. POST data: <pre>!data</pre>', [
        '!data' => print_r($unavailable_required_keys, true),
      ], WATCHDOG_WARNING);

      return false;
    }

    if (!in_array($data['Status'], [
      'AUTHORIZED',
      'CONFIRMED',
      'REJECTED',
      'CANCELED',
      'REVERSED',
      'REFUNDED',
    ])) {
      watchdog('payment_tinkoff', 'Wrong payment status. POST data: <pre>!data</pre>', [
        '!data' => print_r($data, true),
      ], WATCHDOG_WARNING);

      return false;
    }

    $payment = entity_load_single('payment', $data['OrderId']);
    if (!$payment) {
      watchdog('payment_tinkoff', 'Missing payment ID. POST data: !data', [
        '!data' => print_r($data, true),
      ], WATCHDOG_WARNING);

      return false;
    }

    $settings = $payment->method->controller_data;
    if (
      empty(trim($settings[$settings['mode'] === 'test' ? 'test_terminal_login' : 'live_terminal_login'])) ||
      empty(trim($settings[$settings['mode'] === 'test' ? 'test_terminal_pass' : 'live_terminal_pass']))
    ) {
      watchdog('payment_tinkoff !data', 'Wrong terminal settings. POST data: <pre>!data</pre> <pre>!settings</pre>', [
        '!settings' => print_r($settings, true),
        '!data' => print_r($data, true),
      ], WATCHDOG_WARNING);

      return false;
    }

    if ($payment->totalAmount(true) != ((int)$data['Amount'] / 100)) {
      watchdog('payment_tinkoff', 'Wrong payment amount. POST data: !data', [
        '!data' => print_r($data, true),
      ], WATCHDOG_WARNING);

      return false;
    }

    if ($data['Token'] !== self::genToken($settings, $data)) {
      watchdog('payment_tinkoff', 'Wrong payment token. POST data: !data', [
        '!data' => print_r($data, true),
      ], WATCHDOG_WARNING);

      return false;
    }

    return true;
  }

  static public function genToken($settings, $args): string
  {
    if (isset($args['DATA'])) {
      unset($args['DATA']);
    }
    if (isset($args['Receipt'])) {
      unset($args['Receipt']);
    }
    if (isset($args['Token'])) {
      unset($args['Token']);
    }
    if (isset($args['Success'])) {
      $args['Success'] = $args['Success'] ? 'true' : 'false';
    }

    $args['Password'] = trim($settings[$settings['mode'] === 'test' ? 'test_terminal_pass' : 'live_terminal_pass']);
    ksort($args);

    $token = '';

    foreach ($args as $arg) {
      if (!is_array($arg)) {
        $token .= $arg;
      }
    }

    return hash('sha256', $token);
  }
}
