<?php

  /**
   * @file commerce_sagepay_repeat.inc
   * Functions for handling repeat payments.
   */

  module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_common');
  module_load_include('inc', 'commerce_sagepay', 'includes/commerce_sagepay_utils');

  /**
   * Form callback: allows the user to repeat a transaction.
   */
  function commerce_sagepay_repeat_form($form, &$form_state, $order, $transaction) {
    $form_state['order'] = $order;
    $form_state['transaction'] = $transaction;

    // Load and store the payment method instance for this transaction.
    $payment_method = commerce_payment_method_instance_load($transaction->instance_id);
    $form_state['payment_method'] = $payment_method;

    $balance = commerce_payment_order_balance($order);

    // Assume we are going to want to repeat the whole transaction.
    $default_amount = $transaction->amount;

    // Convert the price amount to a user friendly decimal value.
    $default_amount = commerce_currency_amount_to_decimal($default_amount, $transaction->currency_code);

    $description = implode('<br />', array(
      t('Repeat: @amount', array('@amount' => commerce_currency_format($transaction->amount, $transaction->currency_code))),
      t('Order balance: @balance', array('@balance' => commerce_currency_format($balance['amount'], $balance['currency_code']))),
    ));

    $form['amount'] = array(
      '#type' => 'textfield',
      '#title' => t('Repeat amount'),
      '#description' => check_plain($description),
      '#default_value' => $default_amount,
      '#field_suffix' => check_plain($transaction->currency_code),
      '#size' => 16,
    );

    $form = confirm_form($form,
      t('What amount do you want to repeat?'),
      'admin/commerce/orders/' . $order->order_id . '/payment',
      '',
      t('Repeat'),
      t('Cancel'),
      'confirm'
    );

    return $form;
  }

  /**
   * Validate handler: ensure a valid amount is given.
   */
  function commerce_sagepay_repeat_form_validate($form, &$form_state) {
    $transaction = $form_state['transaction'];
    $amount = $form_state['values']['amount'];

    // Ensure a positive numeric amount has been entered for capture.
    if (!is_numeric($amount) || $amount <= 0) {
      form_set_error('amount', t('You must specify a positive numeric amount to repeat.'));
    }

    // Ensure the amount is less than or equal to the transaction amount.
    if ($amount > commerce_currency_amount_to_decimal($transaction->amount, $transaction->currency_code)) {
      form_set_error('amount', t('You cannot repeat more than you originally collected through SagePay.'));
    }

  }

  /**
   * Submit handler: process a prior authorization capture via AIM.
   */
  function commerce_sagepay_repeat_form_submit($form, &$form_state) {
    $transaction = $form_state['transaction'];
    $amount = $form_state['values']['amount'];
    $int_random_number = rand(0, 32000) * rand(0, 32000);
    $repeat_transaction_id = 'REPEAT_' . $transaction->order_id . '_' . $int_random_number;
    $query = array(
      'VPSProtocol' => SAGEPAY_PROTOCOL,
      'TxType' => 'REPEAT',
      'Vendor' => variable_get(SAGEPAY_SETTING_VENDOR_NAME),
      'VendorTxCode' => $repeat_transaction_id,
      'Amount' => $amount,
      'Currency' => $transaction->currency_code,
      'Description' => t('Repeat payment against order %order_id', array('%order_id' => $transaction->order_id)),
      'RelatedVPSTxId' => $transaction->remote_id,
      'RelatedVendorTxCode' => $transaction->payload['VendorTxCode'],
      'RelatedSecurityKey' => $transaction->payload['SecurityKey'],
      'RelatedTxAuthNo' => $transaction->payload['TxAuthNo'],
    );

    switch (variable_get(SAGEPAY_SETTING_TRANSACTION_MODE)) {
      case SAGEPAY_TXN_MODE_LIVE:
        $url = SAGEPAY_SHARED_REPEAT_TRANSACTION_LIVE;
        break;
      case SAGEPAY_TXN_MODE_TEST:
        $url = SAGEPAY_SHARED_REPEAT_TRANSACTION_TEST;
        break;
      case SAGEPAY_TXN_MODE_SIMULATION:
        $url = SAGEPAY_SHARED_REPEAT_TRANSACTION_SIMULATION;
        break;
    }


    $query = _commerce_sagepay_array_to_post($query);

    $response = _commerce_sagepay_request_post($url, $query);

    // Update and save the transaction based on the response.
    $transaction->payload[REQUEST_TIME] = $response;

    // Create a new transaction for the repeat order.
    $repeat_transaction = commerce_payment_transaction_new($transaction->payment_method, $transaction->order_id);
    $repeat_transaction->instance_id = $transaction->instance_id;
    $repeat_transaction->amount = $amount * 100;
    $repeat_transaction->currency_code = $transaction->currency_code;
    $repeat_transaction->remote_id = $repeat_transaction_id;
    $repeat_transaction->payload = $response;
    $repeat_transaction->remote_status = SAGEPAY_REMOTE_STATUS_PAYMENT;

    switch ($response['Status']) {
      case 'OK':
        drupal_set_message(t('Payment repeated successfully.'));
        $repeat_transaction->status = COMMERCE_PAYMENT_STATUS_SUCCESS;
        break;

      default:
        // Display an error message but leave the transaction pending.
        $repeat_transaction->status = COMMERCE_PAYMENT_STATUS_FAILURE;
        drupal_set_message(t('Transaction repeat failed.'), 'error');
        drupal_set_message(check_plain($response['StatusDetail']), 'error');
    }

    $transaction_message = 'Status @status, @statusdetail. ';

    $repeat_transaction->message = $transaction_message;
    $repeat_transaction->message_variables =
      array(
        '@status' => $response['Status'],
        '@statusdetail' => $response['StatusDetail'],
      );


    commerce_payment_transaction_save($repeat_transaction);
    $form_state['redirect'] = 'admin/commerce/orders/' . $form_state['order']->order_id . '/payment';
  }