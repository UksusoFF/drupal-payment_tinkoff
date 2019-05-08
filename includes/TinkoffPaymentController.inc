<?php

class TinkoffPaymentController extends PaymentMethodController
{
  /**
   * Tinkoff server payment URL.
   */
  const SERVER_URL = 'https://securepay.tinkoff.ru/rest';

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
      trim($settings[$settings['mode'] === 'test' ? 'test_terminal_pass' : 'live_terminal_pass']),
      self::SERVER_URL
    );

    try {
      $request = json_decode($tinkoff->init([
        'OrderId' => $payment->pid,
        'Amount' => $payment->totalAmount(true) * 100,
      ]));

      if (empty($request->PaymentURL)) {
        throw new \Exception('Missed PaymentURL. Response data: '. json_encode($request, JSON_PRETTY_PRINT));
      }
    } catch (\Exception $e) {
      watchdog('payment_tinkoff', $e->getMessage() , [], WATCHDOG_ERROR);
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

    $token_data = array_merge($data, [
      'Password' => trim($settings[$settings['mode'] === 'test' ? 'test_terminal_pass' : 'live_terminal_pass']),
    ]);
    ksort($token_data);
    unset($token_data['Token']);
    if ($data['Token'] != hash('sha256', implode('', array_values($token_data)))) {
      watchdog('payment_tinkoff', 'Wrong payment token. POST data: !data', [
        '!data' => print_r($data, true),
      ], WATCHDOG_WARNING);
      return false;
    }

    return true;
  }
}
